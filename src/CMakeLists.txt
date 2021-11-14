add_library(xsim ast.cc)
target_link_libraries(xsim PUBLIC slangcompiler)
# force to use old fashion of span since slang is compiled against C++17
target_compile_definitions(xsim PUBLIC span_CONFIG_SELECT_SPAN=1)