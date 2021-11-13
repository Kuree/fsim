add_executable(xsim-bin xsim.cpp)
set_target_properties(xsim-bin PROPERTIES OUTPUT_NAME xsim)
target_link_libraries(xsim-bin PRIVATE xsim)