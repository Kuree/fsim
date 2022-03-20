add_executable(fsim-bin fsim.cc)
target_link_libraries(fsim-bin PRIVATE fsim ${STATIC_CXX_FLAG})
set_target_properties(fsim-bin PROPERTIES OUTPUT_NAME "fsim")
target_compile_options(fsim-bin PRIVATE -Wall -Werror -Wpedantic)