add_subdirectory(googletest)
add_subdirectory(fmt)
add_subdirectory(marl)

# if we detects slang-dist is included in the extern folder,
# use that instead
if (NOT LIBSLANG_LIBRARY)
    # build from scratch
    MESSAGE("-- Info: building slang from source")
    add_subdirectory(slang)
else()
    MESSAGE("-- Info: using prebuilt slang binaries")
endif()