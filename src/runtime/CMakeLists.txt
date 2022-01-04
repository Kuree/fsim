add_library(xsim-runtime SHARED system_task.cc scheduler.cc module.cc variable.cc)
target_include_directories(xsim-runtime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/fmt/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/marl/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/logic/include)

if (${APPLE})
    target_link_libraries(xsim-runtime PUBLIC fmt marl logic)
else ()
    set(LINUX_LINK_FLAGS "-Wl,--whole-archive $<TARGET_FILE:logic> $<TARGET_FILE:marl> -Wl,--no-whole-archive ${STATIC_CXX_FLAG}")
    # remove trailing space if any
    string(STRIP "${LINUX_LINK_FLAGS}" LINUX_LINK_FLAGS)
    target_link_libraries(xsim-runtime PUBLIC fmt ${LINUX_LINK_FLAGS})
    # force dependencies
    add_dependencies(xsim-runtime marl logic)
endif ()


set_property(TARGET xsim-runtime PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(xsim-runtime PRIVATE -Wall -Werror -Wpedantic -Wno-gnu-zero-variadic-macro-arguments
        -Wno-unknown-attributes -Wno-unknown-warning-option)
# force the extension to be so regardless of the platform
set_target_properties(xsim-runtime PROPERTIES SUFFIX ".so")
