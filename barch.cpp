#include "barch.h"

#include <climits>
#include <fstream>
#include <vector>

namespace Barch
{

static constexpr unsigned char barch_format_id  = 0xBA;
static constexpr unsigned char chunk_size_bytes = 4;
static constexpr unsigned char offset_width     = sizeof(char);
static constexpr unsigned char offset_height    = sizeof(char) + sizeof(int);
static constexpr unsigned char offset_data      = sizeof(char) + sizeof(int) * 2;

using namespace std;

RawImageData RawImage::data() const
{
    return {width, height, raw_data.get()};
}

CompressedImageData CompressedImage::data() const
{
    return {size_bytes, compressed_data.get()};
}

namespace
{

struct Bitter
{
    bool read(size_t bit) const noexcept
    {
        return data[bit / CHAR_BIT] & (1U << (bit % CHAR_BIT));
    }

    void read_chunk(size_t bit, unsigned char* dst, size_t len_bytes) const noexcept
    {
        while (len_bytes--)
        {
            const auto bit_shift = bit % CHAR_BIT;
            *dst = data[bit / CHAR_BIT] >> bit_shift;

            bit += CHAR_BIT;

            *dst |= (data[bit / CHAR_BIT] << (CHAR_BIT - bit_shift)) & 0xFF;
            ++dst;
        }
    }

    void set(size_t bit) noexcept
    {
        data[bit / CHAR_BIT] |= (1U << (bit % CHAR_BIT));
    }

    void clear(size_t bit) noexcept
    {
        data[bit / CHAR_BIT] &= ~(1U << (bit % CHAR_BIT));
    }

    void write_chunk(size_t bit, const unsigned char* src, size_t len_bytes) noexcept
    {
        while (len_bytes--)
        {
            const auto bit_shift = bit % CHAR_BIT;

            data[bit / CHAR_BIT] &= 0xFF >> (CHAR_BIT - bit_shift);
            data[bit / CHAR_BIT] |= *src << bit_shift;

            bit += CHAR_BIT;

            data[bit / CHAR_BIT] &= 0xFF << bit_shift;
            data[bit / CHAR_BIT] |= *src >> (CHAR_BIT - bit_shift);

            ++src;
        }
    }

    unsigned char * data;
};

}

static size_t bytesTaken(size_t bits) noexcept
{
    return bits / CHAR_BIT + (bits % CHAR_BIT != 0);
}

CompressedImage compress(RawImageData image_data)
{
    if (!image_data.data)
    {
        return {0, nullptr};
    }

    if (image_data.height == 0 || image_data.width == 0) // empty image
    {
        auto data = make_unique<unsigned char[]>(offset_height);
        data[0] = barch_format_id;
        int zero = 0;
        memcpy(data.get() + offset_width, &zero, sizeof(int));
        return {offset_height, move(data)};
    }

    vector<unsigned char> buffer(offset_data + bytesTaken(image_data.height));
    buffer[0] = barch_format_id;
    memcpy(buffer.data() + offset_width,  &image_data.width,  sizeof(int));
    memcpy(buffer.data() + offset_height, &image_data.height, sizeof(int));
    size_t buffer_bits = 0;

    const bool got_partial_span = image_data.width % 4 != 0;
    const auto worst_compression_ratio = ((CHAR_BIT * image_data.width + 2.0 * (image_data.width / 4 + got_partial_span))) / (CHAR_BIT * image_data.width);
    const auto max_row_len_bytes = (size_t)ceil(image_data.width * worst_compression_ratio);

    auto row_buffer = make_unique<unsigned char[]>(max_row_len_bytes);
    Bitter row_bits{row_buffer.get()};

    for (int j = 0; j < image_data.height; ++j)
    {
        buffer.resize(offset_data + bytesTaken(image_data.height + buffer_bits) + max_row_len_bytes); // ensure no reallocation occurs during the iteration
        Bitter bits{buffer.data() + offset_data};

        bool all_row_white = true;
        size_t row_buffer_bits = 0;

        auto * raw_row = image_data.data + image_data.width * j;
        for (int i = 0; i < image_data.width; i += 4)
        {
            bool span_white = true;
            bool span_black = true;

            const auto len_bytes = min(image_data.width, i + chunk_size_bytes) - i; // width may not be divisible by 4
            for (int k = 0; k < len_bytes; ++k)
            {
                const auto raw_byte = raw_row[i + k];
                span_white &= raw_byte == 0xFF;
                span_black &= raw_byte == 0;
            }

            all_row_white &= span_white;

            if (span_white) // 0
            {
                row_bits.clear(row_buffer_bits++);
                continue;
            }

            if (span_black) // 10
            {
                row_bits.set(row_buffer_bits++);
                row_bits.clear(row_buffer_bits++);
                continue;
            }

            // 11 ...
            row_bits.set(row_buffer_bits++);
            row_bits.set(row_buffer_bits++);

            row_bits.write_chunk(row_buffer_bits, raw_row + i, len_bytes);
            row_buffer_bits += len_bytes * CHAR_BIT;
        }

        if (all_row_white)
        {
            bits.clear(j);
            continue;
        }

        bits.set(j);
        const auto row_buffer_bytes = bytesTaken(row_buffer_bits);
        bits.write_chunk(image_data.height + buffer_bits, row_buffer.get(), row_buffer_bytes);
        buffer_bits += row_buffer_bits;
    }

    buffer.resize(offset_data + bytesTaken(image_data.height + buffer_bits));
    auto data = make_unique<unsigned char[]>(buffer.size());
    copy(buffer.begin(), buffer.end(), data.get());
    return {buffer.size(), move(data)};
}

RawImage uncompress(CompressedImageData image_data)
{
    if (!image_data.data || image_data.data[0] != barch_format_id || image_data.size_bytes < offset_data)
    {
        return {0, 0, nullptr};
    }

    int width, height;
    memcpy(&width,  image_data.data + offset_width,  sizeof(int));
    memcpy(&height, image_data.data + offset_height, sizeof(int));
    if (image_data.size_bytes < offset_data + bytesTaken(height))
    {
        return {0, 0, nullptr};
    }

    auto raw_data = make_unique<unsigned char[]>((size_t)width * height);
    const Bitter bits{image_data.data + offset_data};

    size_t payload_pos = height;

    for (int j = 0; j < height; ++j)
    {
        auto * const raw_row = raw_data.get() + width * j;

        if (!bits.read(j)) // row is white
        {
            memset(raw_row, 0xFF, width);
            continue;
        }

        for (int i = 0; i < width; i += chunk_size_bytes)
        {
            const auto len_bytes = min(width, i + chunk_size_bytes) - i; // width may not be divisible by 4

            if (image_data.size_bytes < offset_data + payload_pos / CHAR_BIT)
            {
                return {0, 0, nullptr};
            }

            auto bit = bits.read(payload_pos++);
            if (!bit) // ff ff ff ff
            {
                memset(raw_row + i, 0xFF, len_bytes);
                continue;
            }

            if (image_data.size_bytes < offset_data + payload_pos / CHAR_BIT)
            {
                return {0, 0, nullptr};
            }

            bit = bits.read(payload_pos++);
            if (!bit) // 00 00 00 00
            {
                memset(raw_row + i, 0, len_bytes);
                continue;
            }

            if (image_data.size_bytes < offset_data + (payload_pos + len_bytes - 1) / CHAR_BIT)
            {
                return {0, 0, nullptr};
            }

            // ?? ?? ?? ??
            bits.read_chunk(payload_pos, raw_row + i, len_bytes);
            payload_pos += len_bytes * CHAR_BIT;
        }
    }

    return {width, height, move(raw_data)};
}

static string fileName(string_view image_name)
{
    string file_name(image_name);
    file_name += ".barch";
    return file_name;
}

bool saveToDisk(CompressedImageData image_data, string_view image_name)
{
    ofstream file(fileName(image_name), ios::binary);
    return file && file.write(reinterpret_cast<char*>(image_data.data), image_data.size_bytes);
}

CompressedImage loadFromDisk(string_view image_name)
{
    ifstream file(fileName(image_name), ios::binary);
    if (!file)
    {
        return {0, nullptr};
    }

    file.seekg(0, ios::end);
    const streampos size_bytes = file.tellg();
    file.seekg(0, ios::beg);

    auto data = make_unique<unsigned char[]>(size_bytes);
    if (!file.read(reinterpret_cast<char*>(data.get()), size_bytes))
    {
        return {0, nullptr};
    }

    return {(size_t)size_bytes, move(data)};
}

}
