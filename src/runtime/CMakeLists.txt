add_library(xsim-runtime SHARED system_task.cc scheduler.cc module.cc)
target_include_directories(xsim-runtime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/fmt/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/marl/include)
target_link_libraries(xsim-runtime PUBLIC fmt marl -Wl,--whole-archive logic -Wl,--no-whole-archive)
set_property(TARGET xsim-runtime PROPERTY POSITION_INDEPENDENT_CODE ON)