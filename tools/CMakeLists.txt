add_executable(xsim-bin xsim.cc)
target_link_libraries(xsim-bin PRIVATE xsim ${STATIC_CXX_FLAG})
set_target_properties(xsim-bin PROPERTIES OUTPUT_NAME "xsim")
target_compile_options(xsim-bin PRIVATE -Wall -Werror -Wpedantic)