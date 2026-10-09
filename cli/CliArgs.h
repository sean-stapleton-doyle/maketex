#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace maketex::cli {

enum class Command {
    Build,
    Watch,
    Clean,
};

// Raw command-line values. Every field the config file can also supply is an
// optional so the merge layer can tell "flag given" from "not mentioned".
// Enum-like strings (engine, tools) are validated later, in assembleOptions.
struct CliArgs {
    Command command = Command::Build;

    std::optional<std::filesystem::path> texFile;
    std::optional<std::string> engine;
    std::optional<std::string> engineProgram;
    std::vector<std::string> engineOptions;
    std::optional<std::string> tools; // comma-separated
    std::optional<std::filesystem::path> outputDir;
    std::optional<bool> haltOnError;
    std::optional<bool> shellEscape;
    std::optional<bool> synctex;
    std::optional<int> maxPasses;
    std::optional<int> timeoutSeconds;

    bool verbose = false;
    bool plain = false;
    std::optional<std::string> format; // build only: human, json, or github
    std::optional<std::filesystem::path> configPath;
    bool noConfig = false;

    // watch
    std::optional<int> intervalMs;
    bool noTui = false;

    // clean
    bool cleanAll = false;
    bool dryRun = false;
};

// latexmk-style default command: when the first argument names no subcommand
// and isn't a bare --help/--version, "build" is inserted so `maketex foo.tex`
// works. args excludes the program name.
std::vector<std::string> normalizeArgv(std::vector<std::string> args);

// Parsed arguments, or — when CLI11 already handled the invocation (help,
// version, usage error) and printed its message — the exit code for main.
struct ParseResult {
    std::optional<CliArgs> args;
    int exitCode = 0;
};

ParseResult parseCliArgs(const std::vector<std::string>& args);

} // namespace maketex::cli
