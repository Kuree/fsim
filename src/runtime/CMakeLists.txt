if (WIN32)
    set(BUILD_TYPE STATIC)
else()
    set(BUILD_TYPE SHARED)
endif()

add_library(fsim-runtime ${BUILD_TYPE} system_task.cc scheduler.cc module.cc variable.cc vpi.cc)
target_include_directories(fsim-runtime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/fmt/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/marl/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/logic/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/vlstd
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/
        ${CMAKE_BINARY_DIR})

if (UNIX AND NOT APPLE)
    set(LINUX_LINK_FLAGS "-Wl,--whole-archive $<TARGET_FILE:logic> $<TARGET_FILE:marl> -Wl,--no-whole-archive ${STATIC_CXX_FLAG}")
    # remove trailing space if any
    string(STRIP "${LINUX_LINK_FLAGS}" LINUX_LINK_FLAGS)
    target_link_libraries(fsim-runtime PUBLIC fmt fsim-platform ${CMAKE_DL_LIBS} ${LINUX_LINK_FLAGS})
    # force dependencies
    add_dependencies(fsim-runtime marl logic)
elseif (APPLE)
    set(MAC_LINK_FLAGS "-Wl,-force_load,$<TARGET_FILE:logic> -Wl,-force_load,$<TARGET_FILE:marl>")
    target_link_libraries(fsim-runtime PUBLIC fmt fsim-platform ${CMAKE_DL_LIBS} ${MAC_LINK_FLAGS})
    add_dependencies(fsim-runtime marl logic)
else ()
    target_link_libraries(fsim-runtime PUBLIC fmt marl logic fsim-platform ${CMAKE_DL_LIBS})
endif ()


set_property(TARGET fsim-runtime PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(fsim-runtime PRIVATE -fvisibility=default -Wall -Werror -Wpedantic
        -Wno-gnu-zero-variadic-macro-arguments -Wno-unknown-attributes -Wno-unknown-warning-option
        -Wno-microsoft-enum-value -Wno-language-extension-token)

if (NOT WIN32)
    set_target_properties(fsim-runtime PROPERTIES SUFFIX ".so")
endif ()

# generate version files
file(READ ${CMAKE_SOURCE_DIR}/VERSION FSIM_VERSION)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.hh.in ${CMAKE_BINARY_DIR}/version.hh)
