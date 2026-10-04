foreach(target test_search test_stream test_walk)
    add_executable(${target} ${target}.c)
    target_link_libraries(${target} PRIVATE grepmorph_core)
    grepmorph_set_warnings(${target})

    if(UNIX)
        target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    if(GREPMORPH_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endforeach()

foreach(case basic binary overlap boundaries invalid tiny_allocations morph_labels)
    add_test(NAME search_${case} COMMAND test_search ${case})
endforeach()

foreach(case boundary overlap long_pattern empty_and_short binary invalid offsets_64
             offset_overflow no_sink differential)
    add_test(NAME stream_${case} COMMAND test_stream ${case}
        "${CMAKE_CURRENT_BINARY_DIR}/stream-${case}.bin")
endforeach()

# Invalid CRT descriptors can invoke Windows' invalid-parameter handler, so
# descriptor-failure injection is POSIX-only rather than weakening the test.
if(UNIX)
    add_test(NAME stream_read_error COMMAND test_stream read_error
        "${CMAKE_CURRENT_BINARY_DIR}/stream-read_error.bin")
endif()

add_test(NAME cli_help COMMAND grepmorph --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")
add_test(NAME cli_version COMMAND grepmorph --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "0\\.1\\.0")

foreach(case match no_match missing_file empty_query no_arguments missing_path unknown_option
             hex hex_binary hex_odd hex_invalid hex_empty multiple_files error_precedence
             dash_query directory literal_backslash empty_file short_file chunk_boundary chunk_overlap)
    add_test(NAME cli_${case} COMMAND ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:grepmorph>"
        "-DCASE=${case}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-${case}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/test_cli.cmake")
endforeach()

set(walk_cases basic invalid order wide stop vanished opener)
if(UNIX)
    list(APPEND walk_cases links special permissions deep_low_descriptors)
endif()
foreach(case IN LISTS walk_cases)
    add_test(NAME walk_${case} COMMAND ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:test_walk>"
        "-DCASE=${case}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/walk-${case}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_walk.cmake")
    set_tests_properties(walk_${case} PROPERTIES
        TIMEOUT 30 SKIP_REGULAR_EXPRESSION "SKIP:")
endforeach()

foreach(case nested long_option single_file no_match empty hidden mixed_paths error_precedence
             hex chunk_boundary trailing_separator dot parent dash_query duplicate_option
             links explicit_dir_link explicit_file_link dangling_link)
    add_test(NAME recursive_${case} COMMAND ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:grepmorph>"
        "-DCASE=${case}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/recursive-${case}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/test_recursive.cmake")
    set_tests_properties(recursive_${case} PROPERTIES
        TIMEOUT 30 SKIP_REGULAR_EXPRESSION "SKIP:")
endforeach()
