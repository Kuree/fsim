function(add_warning target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else ()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-error=attributes)
    endif ()
endfunction()

set(CODEGEN_SRC codegen/cxx.cc codegen/expr.cc codegen/ninja.cc codegen/stmt.cc codegen/util.cc)
set(IR_SRC ir/ast.cc ir/ir.cc)

add_library(xsim builder.cc ${CODEGEN_SRC} ${IR_SRC})
target_link_libraries(xsim PUBLIC slangcompiler)
# use some libraries already used for slang to avoid linking errors
target_include_directories(xsim PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../extern/slang/external
        ${CMAKE_CURRENT_SOURCE_DIR}/../extern/cpp-subprocess)
# force to use old fashion of span since slang is compiled against C++17
target_compile_definitions(xsim PUBLIC span_CONFIG_SELECT_SPAN=1)
add_warning(xsim)

# runtime library
add_subdirectory(runtime)
