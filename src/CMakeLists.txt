add_library(xsim ast.cc)
target_include_directories(xsim PUBLIC
        ../extern/slang/include)
target_link_libraries(xsim PUBLIC slangcompiler)