function(add_test target)
    add_executable(${target} ${target}.cc)
    set(extra_lib ${ARGN})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/googletest/googletest/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/slang/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/logic/include)
    target_link_libraries(${target} PRIVATE gtest gtest_main xsim ${extra_lib})
    gtest_discover_tests(${target})
endfunction()

add_test(test_ast)
add_test(test_ir)
add_test(test_builder)

add_subdirectory(runtime)