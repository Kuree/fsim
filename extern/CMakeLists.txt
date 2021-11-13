add_subdirectory(slang)
# slang specific flags
target_compile_definitions(slangcompiler PRIVATE DEBUG)
add_subdirectory(googletest)