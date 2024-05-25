cmake_minimum_required(VERSION 3.28)
project(bencode_c C)

set(CMAKE_C_STANDARD 11)

find_package(Python3 COMPONENTS Development)

# add python includes to find <Python.h>
include_directories(${Python3_INCLUDE_DIRS})

include_directories(src/bencode_c)

include_directories(vendor/klib)
include_directories(vendor/hashmap)

add_executable(
        bencode_c
        src/bencode_c/overflow.h
        src/bencode_c/common.h
        src/bencode_c/bencode.c
        src/bencode_c/decode.c
        src/bencode_c/encode.c
        src/bencode_c/str.h
        src/bencode_c/ctx.h
)
