function(add_warning target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else ()
        if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            set(WNO_FLAG "-Wno-gnu-zero-variadic-macro-arguments")
        endif ()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-attributes "${WNO_FLAG}")
    endif ()
endfunction()

set(CODEGEN_SRC codegen/cxx.cc codegen/expr.cc codegen/ninja.cc codegen/stmt.cc codegen/util.cc codegen/dpi.cc)
set(IR_SRC ir/ast.cc ir/ir.cc ir/except.cc)
set(BUILDER_SRC builder/builder.cc builder/dvpi.cc builder/util.cc)

add_library(fsim ${BUILDER_SRC} ${CODEGEN_SRC} ${IR_SRC})
target_link_libraries(fsim PRIVATE slangcompiler ${CMAKE_DL_LIBS} reproc++ marl)
# use some libraries already used for slang to avoid linking errors
target_include_directories(fsim PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../extern/slang/external
        ${CMAKE_CURRENT_SOURCE_DIR}/../extern/reproc/reproc++/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../extern/marl/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../extern/slang/include
        ${CMAKE_BINARY_DIR}/extern/slang/source/)
# force to use old fashion of span since slang is compiled against C++17
target_compile_definitions(fsim PUBLIC span_CONFIG_SELECT_SPAN=1)
set_property(TARGET fsim PROPERTY POSITION_INDEPENDENT_CODE ON)
add_warning(fsim)

# runtime library
add_subdirectory(runtime)
