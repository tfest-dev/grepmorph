foreach(target test_search test_stream test_walk test_morph test_multi_stream test_text_cli test_hex_numeric)
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

foreach(case ascii bmp supplementary binary_and_bom truncated overlong invalid_scalar
             invalid_continuation selection_and_ownership invalid_arguments no_normalisation all_scalars)
    add_test(NAME morph_${case} COMMAND test_morph ${case})
endforeach()
set_tests_properties(morph_all_scalars PROPERTIES TIMEOUT 120)

foreach(case text_boundaries tails_and_overlaps order_and_identical high_offsets invalid
             differential no_sink)
    add_test(NAME multi_stream_${case} COMMAND test_multi_stream ${case}
        "${CMAKE_CURRENT_BINARY_DIR}/multi-stream-${case}.bin")
endforeach()

target_sources(test_text_cli PRIVATE ../src/cli.c)
foreach(case invalid_text invalid_selected unicode raw_invalid_utf8)
    add_test(NAME text_cli_${case} COMMAND test_text_cli ${case}
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/text-unicode.bin")
endforeach()
set_tests_properties(text_cli_invalid_text text_cli_invalid_selected PROPERTIES
    PASS_REGULAR_EXPRESSION "invalid UTF-8 query at byte 1")
set_tests_properties(text_cli_unicode PROPERTIES PASS_REGULAR_EXPRESSION "utf16-be")

set(text_cases bundle utf8 le be subset_order raw_utf8 raw default_raw text_hex hex_text
    morph_hex hex_morph text_morph morph_text duplicate_text duplicate_morph missing_morph
    unsupported_morph empty_text no_match case_sensitive recursive error_precedence missing_path
    dash_query odd_offset truncated_wide no_terminator)
if(UNIX)
    list(APPEND text_cases unicode)
endif()
foreach(case IN LISTS text_cases)
    add_test(NAME text_${case} COMMAND ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:grepmorph>"
        "-DCASE=${case}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/text-${case}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/test_text.cmake")
endforeach()

foreach(case hex_bytes hex_all_bytes hex_overflow compare_rules tiny_compare integer_widths
             integer_ranges integer_syntax integer_exhaustive selection_and_ownership
             stream_hex stream_integers stream_invalid_rule)
    add_test(NAME hex_numeric_${case} COMMAND test_hex_numeric ${case}
        "${CMAKE_CURRENT_BINARY_DIR}/hex-numeric-${case}.bin")
endforeach()
set_tests_properties(hex_numeric_integer_exhaustive hex_numeric_selection_and_ownership PROPERTIES TIMEOUT 120)

foreach(case
    hex_literal hex_binary hex_binary_reordered hex_and_raw
    hex_is_not_input_decoding raw_stays_case_sensitive hex_numeric_lexeme uint16_le
    uint16_be uint32_le uint32_be uint64_le
    uint64_be uint8 integer_hex_lexeme integer_upper_prefix
    leading_zero_is_decimal uint64_max uint64_max_hex all_fitting_integer_widths
    mixed_text_numeric numeric_boundary no_match u8_overflow
    u16_overflow u32_overflow u64_overflow u64_hex_overflow
    negative positive_sign space float
    exponent separator no_prefix empty_prefix
    narrow_width_not_omitted widthless_rejected signed_rejected duplicate_morph
    hex_integer_conflict integer_hex_conflict hex_unicode_conflict text_integer_conflict
    recursive error_precedence hex_overlap hex_separators_not_ignored
    hex_boundary)
    add_test(NAME values_${case} COMMAND ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:grepmorph>"
        "-DCASE=${case}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/values-${case}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/test_values.cmake")
endforeach()
