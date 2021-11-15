add_library(xsim ast.cc ir.cc codegen.cc builder.cc)
target_link_libraries(xsim PUBLIC slangcompiler)
# use some libraries already used for slang to avoid linking errors
target_include_directories(xsim PUBLIC ../extern/slang/external)
# force to use old fashion of span since slang is compiled against C++17
target_compile_definitions(xsim PUBLIC span_CONFIG_SELECT_SPAN=1)

# runtime library
add_subdirectory(runtime)
