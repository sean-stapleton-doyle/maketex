cmake_minimum_required(VERSION 3.25)
project(maketex VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ───────────────────────────────────────────────────────────────────
# Everything beyond the core library defaults off when maketex is consumed by
# another project (add_subdirectory / CPM / FetchContent).
option(MAKETEX_BUILD_CLI "Build the maketex command line interface" ${PROJECT_IS_TOP_LEVEL})
option(MAKETEX_BUILD_HARNESS "Build the end-to-end test harness" ${PROJECT_IS_TOP_LEVEL})
option(MAKETEX_BUILD_TESTS "Build the maketex tests" ${PROJECT_IS_TOP_LEVEL})

if(PROJECT_IS_TOP_LEVEL)
    # Symlink compile_commands.json to the source root so clangd finds it.
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()

# ── CPM ───────────────────────────────────────────────────────────────────────
set(CPM_DOWNLOAD_VERSION 0.40.2)
set(CPM_PATH ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake)
if(NOT EXISTS ${CPM_PATH})
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
        ${CPM_PATH})
endif()
include(${CPM_PATH})

# ── Dependencies ──────────────────────────────────────────────────────────────
CPMAddPackage(
    NAME BLAKE3
    GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3
    GIT_TAG 1.8.2
    SOURCE_SUBDIR c
)
CPMAddPackage("gh:SRombauts/SQLiteCpp#3.3.2")

if(MAKETEX_BUILD_TESTS)
    CPMAddPackage("gh:google/googletest#v1.16.0")
endif()

# The tests cover the CLI modules, so they need the CLI dependencies too.
if(MAKETEX_BUILD_CLI OR MAKETEX_BUILD_TESTS)
    CPMAddPackage(
        NAME ftxui
        GITHUB_REPOSITORY ArthurSonzogni/FTXUI
        GIT_TAG v6.1.9
        OPTIONS
            "FTXUI_BUILD_EXAMPLES OFF"
            "FTXUI_BUILD_DOCS OFF"
            "FTXUI_BUILD_TESTS OFF"
    )
    CPMAddPackage("gh:CLIUtils/CLI11@2.5.0")
    CPMAddPackage("gh:marzer/tomlplusplus#v3.4.0")
    CPMAddPackage("gh:nlohmann/json@3.12.0")
endif()

# ── Core library (shared by executable and tests) ─────────────────────────────
add_library(maketex STATIC
    src/StateDatabase.cpp
    src/PathCodec.cpp
    src/Process.cpp
    src/EngineBase.cpp
    src/ToolContext.cpp
    src/ToolFactory.cpp
    src/engines/PdfLatexEngine.cpp
    src/engines/XeLatexEngine.cpp
    src/engines/LuaLatexEngine.cpp
    src/tools/BibtexTool.cpp
    src/tools/HashTriggeredTool.cpp
    src/DiagnosticExtractor.cpp
    src/DiagnosticFormatter.cpp
    src/Blake3Hasher.cpp
    src/FileStateTracker.cpp
    src/parsers/FlsParser.cpp
    src/parsers/LogParser.cpp
    src/parsers/AuxParser.cpp
    src/graph/FileNode.cpp
    src/graph/RuleNode.cpp
    src/graph/BuildGraph.cpp
    src/BuildDriver.cpp
    src/engines/EngineFactory.cpp
    src/Build.cpp
)
add_library(maketex::maketex ALIAS maketex)

target_include_directories(maketex PUBLIC include)

target_link_libraries(maketex PUBLIC
    BLAKE3::blake3
    SQLiteCpp
)

# Recorded (informationally) in the state database's meta table.
target_compile_definitions(maketex PRIVATE
    MAKETEX_VERSION="${PROJECT_VERSION}"
)

target_compile_options(maketex PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

# ── End-to-end harness (not the CLI; see cli/) ───────────────────────────────
if(MAKETEX_BUILD_HARNESS)
    add_executable(maketex_cli apps/main.cpp)
    set_target_properties(maketex_cli PROPERTIES OUTPUT_NAME maketex-harness)

    target_link_libraries(maketex_cli PRIVATE maketex::maketex)

    target_compile_options(maketex_cli PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
    )
endif()

# ── Command line interface ────────────────────────────────────────────────────
if(MAKETEX_BUILD_CLI OR MAKETEX_BUILD_TESTS)
    add_subdirectory(cli)
endif()

# ── Formatting ───────────────────────────────────────────────────────────────
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT AND PROJECT_IS_TOP_LEVEL)
    file(GLOB_RECURSE ALL_SOURCES
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.h
        ${CMAKE_SOURCE_DIR}/include/*.h
        ${CMAKE_SOURCE_DIR}/apps/*.cpp
        ${CMAKE_SOURCE_DIR}/cli/*.cpp
        ${CMAKE_SOURCE_DIR}/cli/*.h
        ${CMAKE_SOURCE_DIR}/test/*.cpp
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCES}
        COMMENT "Running clang-format"
    )
    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --dry-run --Werror ${ALL_SOURCES}
        COMMENT "Checking clang-format"
    )
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────
if(MAKETEX_BUILD_TESTS)
    enable_testing()

    file(GLOB_RECURSE TEST_SOURCES test/*.cpp)
    add_executable(maketex_tests ${TEST_SOURCES})

    target_link_libraries(maketex_tests PRIVATE
        maketex
        maketex_cli_core
        GTest::gtest_main
    )

    target_compile_options(maketex_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
    )

    include(GoogleTest)
    gtest_discover_tests(maketex_tests)
endif()
