add_subdirectory(googletest)
add_subdirectory(fmt)
add_subdirectory(marl)
add_subdirectory(logic)

# if we detects slang-dist is included in the extern folder,
# use that instead
if (NOT LIBSLANG_LIBRARY)
    # build from scratch
    MESSAGE("-- Info: building slang from source")
    add_subdirectory(slang)
else()
    MESSAGE("-- Info: using prebuilt slang binaries")
endif()

# format lib
set_property(TARGET fmt PROPERTY POSITION_INDEPENDENT_CODE ON)

# reproc
set(REPROC++ ON)
add_subdirectory(reproc)
set_property(TARGET reproc PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET reproc++ PROPERTY POSITION_INDEPENDENT_CODE ON)
