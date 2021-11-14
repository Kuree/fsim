function(add_test target)
    add_executable(${target} ${target}.cc)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/googletest/googletest/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../extern/slang/include)
    target_link_libraries(${target} PRIVATE gtest gtest_main xsim)
    gtest_discover_tests(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_test(test_ast)
