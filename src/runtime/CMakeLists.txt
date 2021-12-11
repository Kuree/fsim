add_library(xsim-runtime SHARED system_task.cc scheduler.cc module.cc variable.cc)
target_include_directories(xsim-runtime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/fmt/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/marl/include)
target_link_libraries(xsim-runtime PUBLIC fmt marl -Wl,--whole-archive logic -Wl,--no-whole-archive ${STATIC_CXX_FLAG})
set_property(TARGET xsim-runtime PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(xsim-runtime PRIVATE -Wall -Werror -Wpedantic -Wno-gnu-zero-variadic-macro-arguments)