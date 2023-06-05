#ifndef BARCH_H
#define BARCH_H

#include <cstddef>
#include <memory>
#include <string_view>

namespace Barch
{
    struct RawImageData {
        int width; // image width in pixels
        int height; // image height in pixels
        unsigned char * data; // Pointer to image data. data[j * width + i] is color of pixel in row j and column i.
    };

    struct RawImage
    {
        int width;
        int height;
        std::unique_ptr<unsigned char[]> raw_data;

        RawImageData data() const;
    };

    struct CompressedImageData {
        std::size_t size_bytes;
        unsigned char * data; // Pointer to compressed image data
    };

    struct CompressedImage
    {
        std::size_t size_bytes;
        std::unique_ptr<unsigned char[]> compressed_data;

        CompressedImageData data() const;
    };

    CompressedImage compress(RawImageData image_data);
    RawImage uncompress(CompressedImageData image_data); // data will be null on error

    bool saveToDisk(CompressedImageData image_data, std::string_view image_name); // saves image to file <image_name>.barch
    CompressedImage loadFromDisk(std::string_view image_name); // loads image from <image_name>.barch, data will be null on error
}

#endif // BARCH_H
