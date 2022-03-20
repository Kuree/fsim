# check if the compiler support static lib gcc
# only if we have static flag on
if (STATIC_BUILD AND (NOT WIN32))
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

if (WIN32)
    # Windows has lots of rules about deprecated functions
    set(CMAKE_CXX_FLAGS "-Wno-deprecated-declarations -D_CRT_SECURE_NO_WARNINGS -DWIN32=1 -Wno-ignored-attributes -Wno-sign-conversion -Wno-zero-as-null-pointer-constant")
endif()