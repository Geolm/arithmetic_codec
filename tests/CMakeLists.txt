cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)

project(arithmetic_codec_unit_tests)

add_executable(test test.c arithmetic_codec.c)

if(MSVC)
    target_compile_options(test PRIVATE /W4 /WX /std:c17)
else()
    target_compile_options(test PRIVATE -Wall -Wextra -Wpedantic -Werror -mavx2 -mfma)
endif()