add_library(fsim-runtime SHARED system_task.cc scheduler.cc module.cc variable.cc vpi.cc)
target_include_directories(fsim-runtime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/fmt/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/marl/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/logic/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/vlstd
        ${CMAKE_CURRENT_SOURCE_DIR}/../builder/
        ${CMAKE_BINARY_DIR})

if (${APPLE})
    target_link_libraries(fsim-runtime PUBLIC fmt marl logic fsim ${CMAKE_DL_LIBS})
else ()
    set(LINUX_LINK_FLAGS "-Wl,--whole-archive $<TARGET_FILE:logic> $<TARGET_FILE:marl> -Wl,--no-whole-archive ${STATIC_CXX_FLAG}")
    # remove trailing space if any
    string(STRIP "${LINUX_LINK_FLAGS}" LINUX_LINK_FLAGS)
    target_link_libraries(fsim-runtime PUBLIC fmt fsim ${CMAKE_DL_LIBS} ${LINUX_LINK_FLAGS})
    # force dependencies
    add_dependencies(fsim-runtime marl logic)
endif ()


set_property(TARGET fsim-runtime PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(fsim-runtime PRIVATE -Wall -Werror -Wpedantic -Wno-gnu-zero-variadic-macro-arguments
        -Wno-unknown-attributes -Wno-unknown-warning-option)
# force the extension to be so regardless of the platform
set_target_properties(fsim-runtime PROPERTIES SUFFIX ".so")

# generate version files
file (READ ${CMAKE_SOURCE_DIR}/VERSION FSIM_VERSION)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.hh.in ${CMAKE_BINARY_DIR}/version.hh)
