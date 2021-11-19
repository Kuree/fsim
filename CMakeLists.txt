cmake_minimum_required(VERSION 3.12)
project(xsim)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# slang specific flags
if (CMAKE_BUILD_TYPE MATCHES "Debug")
    add_compile_options(-DDEBUG)
endif()

# import cmake find
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# external library
# need to handle external slang prebuilt
find_package(Slang)
add_subdirectory(extern)

add_subdirectory(src)
add_subdirectory(tools)


# tests
include(GoogleTest)
include (CTest)
enable_testing()
add_subdirectory(tests)