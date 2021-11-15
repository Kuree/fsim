add_library(xsim-runtime system_task.cc)
target_include_directories(xsim-runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/logic/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../extern/fmt/include)
target_link_libraries(xsim-runtime PRIVATE fmt)
