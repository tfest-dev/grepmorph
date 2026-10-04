add_executable(test_search test_search.c)
target_link_libraries(test_search PRIVATE grepmorph_core)
grepmorph_set_warnings(test_search)

if(GREPMORPH_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(test_search PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(test_search PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME search_exact COMMAND test_search)
add_test(NAME cli_help COMMAND grepmorph --help)
add_test(NAME cli_version COMMAND grepmorph --version)
