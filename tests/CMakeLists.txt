function(add_test target)
    add_executable(${target} ${target}.cc)
    set(extra_lib ${ARGN})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/googletest/googletest/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/logic/include)
    target_link_libraries(${target} PRIVATE gtest gtest_main fsim ${extra_lib})
    gtest_discover_tests(${target})
    target_compile_options(${target} PRIVATE "-Wno-attributes")
    # because the test will load runtime, make sure the test is built after the runtime
    add_dependencies(${target} fsim-runtime)
endfunction()

add_test(test_ast)
add_test(test_ir)
add_test(test_builder)
add_test(test_codegen)

add_subdirectory(runtime)