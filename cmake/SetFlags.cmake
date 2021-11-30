# check if the compiler support static lib gcc
# only if we have static flag on
if (STATIC_BUILD)
    CHECK_CXX_COMPILER_FLAG(-static-libgcc COMPILER_STATIC_LIBGCC)
    if (${COMPILER_STATIC_LIBGCC})
        set(STATIC_GCC_FLAG "-static-libgcc")
    endif ()
    CHECK_CXX_COMPILER_FLAG(-static-libstdc++ COMPILER_STATIC_LIBCXX)
    if (${COMPILER_STATIC_LIBCXX})
        set(STATIC_CXX_FLAG "${STATIC_CXX_FLAG} -static-libstdc++")
    endif ()
    string(STRIP ${STATIC_CXX_FLAG} STATIC_CXX_FLAG)
endif ()