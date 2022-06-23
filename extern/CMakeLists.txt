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
    if (WIN32)
        get_target_property(EXTLIB_COMPILE_FLAGS slangcore COMPILE_OPTIONS )
        list(REMOVE_ITEM EXTLIB_COMPILE_FLAGS -Werror)
        set_target_properties(slangcore PROPERTIES COMPILE_OPTIONS "${EXTLIB_COMPILE_FLAGS}")
    endif ()
else ()
    MESSAGE("-- Info: using prebuilt slang binaries")
endif ()

# format lib
set_property(TARGET fmt PROPERTY POSITION_INDEPENDENT_CODE ON)

# reproc
set(REPROC++ ON)
add_subdirectory(reproc)
set_property(TARGET reproc PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET reproc++ PROPERTY POSITION_INDEPENDENT_CODE ON)
