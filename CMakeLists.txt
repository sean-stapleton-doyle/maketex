cmake_minimum_required(VERSION 3.25)
project(maketex VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Symlink compile_commands.json to the source root so clangd finds it.
execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
)

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
CPMAddPackage("gh:google/googletest#v1.16.0")
CPMAddPackage(
    NAME taskflow
    GIT_REPOSITORY https://github.com/taskflow/taskflow
    GIT_TAG v3.10.0
    OPTIONS
        "TF_BUILD_TESTS OFF"
        "TF_BUILD_EXAMPLES OFF"
)
# Taskflow uses std::is_trivial which is deprecated in C++26.
# Mark its headers as SYSTEM so the deprecation warnings don't surface in our builds.
if(taskflow_ADDED)
    get_target_property(_tf_inc Taskflow INTERFACE_INCLUDE_DIRECTORIES)
    set_property(TARGET Taskflow PROPERTY
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_tf_inc}")
endif()

# ── Core library (shared by executable and tests) ─────────────────────────────
add_library(maketex_lib STATIC
    src/StateDatabase.cpp
    src/Process.cpp
    src/EngineBase.cpp
    src/ToolContext.cpp
    src/ToolFactory.cpp
    src/ToolRunner.cpp
    src/engines/PdfLatexEngine.cpp
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
)

target_include_directories(maketex_lib PUBLIC include)

target_link_libraries(maketex_lib PUBLIC
    BLAKE3::blake3
    SQLiteCpp
    Taskflow::Taskflow
)

target_compile_options(maketex_lib PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(maketex src/main.cpp)

target_link_libraries(maketex PRIVATE maketex_lib)

target_compile_options(maketex PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

# ── Formatting ───────────────────────────────────────────────────────────────
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCES
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.h
        ${CMAKE_SOURCE_DIR}/include/*.h
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
enable_testing()

file(GLOB_RECURSE TEST_SOURCES test/*.cpp)

if(TEST_SOURCES)
    add_executable(maketex_tests ${TEST_SOURCES})

    target_link_libraries(maketex_tests PRIVATE
        maketex_lib
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
