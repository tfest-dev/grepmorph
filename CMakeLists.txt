cmake_minimum_required(VERSION 3.20)

project(grepmorph VERSION 0.1.0 LANGUAGES C)

option(GREPMORPH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(GREPMORPH_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviourSanitizer" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(grepmorph_core
    src/morph.c
    src/search.c
    src/file_search.c
    src/walk.c
)

target_include_directories(grepmorph_core PUBLIC src)

if(UNIX)
    target_compile_definitions(grepmorph_core PRIVATE
        _POSIX_C_SOURCE=200809L
        _FILE_OFFSET_BITS=64
    )
endif()

if(APPLE)
    # Darwin hides O_NOFOLLOW when only _POSIX_C_SOURCE is selected.
    # Enable the declared flag; never replace it with zero or drop protection.
    target_compile_definitions(grepmorph_core PRIVATE _DARWIN_C_SOURCE=1)
endif()

add_executable(grepmorph
    src/main.c
    src/cli.c
)

target_link_libraries(grepmorph PRIVATE grepmorph_core)

function(grepmorph_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(GREPMORPH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
        )
        if(GREPMORPH_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

grepmorph_set_warnings(grepmorph_core)
grepmorph_set_warnings(grepmorph)

if(GREPMORPH_ENABLE_SANITIZERS AND NOT MSVC)
    foreach(target grepmorph_core grepmorph)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

enable_testing()
add_subdirectory(tests)
