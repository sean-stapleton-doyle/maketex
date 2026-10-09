# ── CLI core (everything except main; linked by the app and the tests) ───────
add_library(maketex_cli_core STATIC
    CliArgs.cpp
    Config.cpp
    EffectiveOptions.cpp
    CleanPlan.cpp
    FileWatcher.cpp
    render/InlineReport.cpp
    render/JsonReport.cpp
    render/GithubReport.cpp
    render/SourceSnippet.cpp
    commands/BuildCommand.cpp
    commands/WatchCommand.cpp
    commands/CleanCommand.cpp
)

target_include_directories(maketex_cli_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(maketex_cli_core PUBLIC
    maketex::maketex
    CLI11::CLI11
    tomlplusplus::tomlplusplus
    ftxui::component
    nlohmann_json::nlohmann_json
)

target_compile_definitions(maketex_cli_core PUBLIC
    MAKETEX_VERSION="${PROJECT_VERSION}"
)

target_compile_options(maketex_cli_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
)

# ── The user-facing maketex binary ────────────────────────────────────────────
# maketex_cli_core above is also built for MAKETEX_BUILD_TESTS-only
# configurations (the unit tests cover it); the binary itself is CLI-only.
if(MAKETEX_BUILD_CLI)
    add_executable(maketex_app main.cpp)
    set_target_properties(maketex_app PROPERTIES OUTPUT_NAME maketex)

    target_link_libraries(maketex_app PRIVATE maketex_cli_core)

    target_compile_options(maketex_app PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
    )
endif()
