# slang specific flags
if (CMAKE_BUILD_TYPE MATCHES "Debug")
    add_compile_options(-DDEBUG)
endif()
add_subdirectory(slang)
add_subdirectory(googletest)