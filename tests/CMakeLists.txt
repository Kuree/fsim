function(add_test target)
    add_executable(${target} ${target}.cc)
    set(extra_lib ${ARGN})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/googletest/googletest/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/slang/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/logic/include)
    target_link_libraries(${target} PRIVATE gtest gtest_main xsim ${extra_lib})
    gtest_discover_tests(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_test(test_ast)
add_test(test_ir)

add_subdirectory(runtime)