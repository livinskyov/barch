cmake_minimum_required(VERSION 3.14)

project(barch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(barch STATIC
  barch.cpp
  barch.h
)

target_compile_definitions(barch PRIVATE BARCH_LIBRARY)
