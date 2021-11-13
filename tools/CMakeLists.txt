add_executable(xsim-bin xsim.cc)
target_link_libraries(xsim-bin PRIVATE xsim)
set_target_properties(xsim-bin PROPERTIES OUTPUT_NAME "xsim")
