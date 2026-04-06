cmake_minimum_required(VERSION 3.16)
project(sqlite_latin1 VERSION 1.0.0 LANGUAGES C)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BUILD_SHARED_EXT "Build the loadable SQLite extension (.so/.dylib)" ON)

# ---------------------------------------------------------------------------
# Find SQLite3
# ---------------------------------------------------------------------------
find_package(SQLite3 REQUIRED)
message(STATUS "SQLite3: ${SQLite3_VERSION} – include: ${SQLite3_INCLUDE_DIRS}")

# ---------------------------------------------------------------------------
# Static library (compiled with SQLITE_CORE so all sqlite3_* calls resolve
# directly to the linked libsqlite3, no extension API indirection needed).
# Used by the test binary.
# ---------------------------------------------------------------------------
add_library(latin1_static STATIC src/latin1.c)
target_include_directories(latin1_static
    PUBLIC  src
    PRIVATE ${SQLite3_INCLUDE_DIRS})
target_compile_definitions(latin1_static PRIVATE SQLITE_CORE)
target_link_libraries(latin1_static PUBLIC SQLite3::SQLite3)

# Strict warnings
if(MSVC)
    target_compile_options(latin1_static PRIVATE /W4 /WX)
else()
    target_compile_options(latin1_static PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------------------
# Loadable extension (MODULE – not linkable, only dlopen-able).
# Compiled WITHOUT SQLITE_CORE so it uses the sqlite3ext.h indirection and
# exports the sqlite3_latin1_init() entry point.
# ---------------------------------------------------------------------------
if(BUILD_SHARED_EXT)
    add_library(latin1 MODULE src/latin1.c)
    target_include_directories(latin1
        PRIVATE src
        PRIVATE ${SQLite3_INCLUDE_DIRS})
    # Do NOT link against SQLite3 – symbols are resolved at load time by the
    # host process (the SQLite3 executable or embedding application).
    set_target_properties(latin1 PROPERTIES
        PREFIX ""          # produces latin1.so / latin1.dylib
        C_VISIBILITY_PRESET default)
    # SQLite extension symbols are resolved by the host process at load time.
    if(APPLE)
        target_link_options(latin1 PRIVATE -undefined dynamic_lookup)
    elseif(UNIX)
        target_link_options(latin1 PRIVATE -Wl,--allow-shlib-undefined)
    endif()
    if(MSVC)
        target_compile_options(latin1 PRIVATE /W4)
    else()
        target_compile_options(latin1 PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    message(STATUS "Loadable extension target: latin1")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_latin1 test/test_latin1.c)
target_include_directories(test_latin1 PRIVATE src ${SQLite3_INCLUDE_DIRS})
target_link_libraries(test_latin1 PRIVATE latin1_static)

if(MSVC)
    target_compile_options(test_latin1 PRIVATE /W4 /WX)
else()
    target_compile_options(test_latin1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME latin1_suite COMMAND test_latin1)

# ---------------------------------------------------------------------------
# Fuzz target (optional – requires clang with LibFuzzer support)
#
# Usage:
#   cmake -B build -DENABLE_FUZZING=ON \
#         -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang
#   cmake --build build --target fuzz_latin1
#   ./build/fuzz_latin1 -max_total_time=60
# ---------------------------------------------------------------------------
option(ENABLE_FUZZING "Build LibFuzzer fuzz target (requires clang)" OFF)

if(ENABLE_FUZZING)
    add_executable(fuzz_latin1 test/fuzz_latin1.c)
    target_include_directories(fuzz_latin1 PRIVATE src ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(fuzz_latin1 PRIVATE latin1_static)
    target_compile_options(fuzz_latin1 PRIVATE -fsanitize=fuzzer,address -g)
    target_link_options(fuzz_latin1 PRIVATE -fsanitize=fuzzer,address)
    message(STATUS "Fuzz target: fuzz_latin1")
    message(STATUS "  Run with: ./build/fuzz_latin1 -max_total_time=60")
endif()

# Print a reminder on how to run tests
message(STATUS "Build with:  cmake --build <build_dir>")
message(STATUS "Test  with:  ctest --test-dir <build_dir> -V")
