#include "CliArgs.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <string_view>

namespace maketex::cli {

std::vector<std::string> normalizeArgv(std::vector<std::string> args)
{
    if (args.empty())
        return { "build" }; // config `main` may still name the document

    const std::string& first = args.front();
    static constexpr std::string_view kSubcommands[] = { "build", "watch", "clean" };
    bool isSubcommand = std::ranges::find(kSubcommands, first) != std::ranges::end(kSubcommands);
    bool isGlobalFlag = first == "--help" || first == "-h" || first == "--version";

    if (!isSubcommand && !isGlobalFlag)
        args.insert(args.begin(), "build");
    return args;
}

namespace {

    // Options accepted by build and watch alike, bound straight into out.
    void addBuildOptions(CLI::App& sub, CliArgs& out)
    {
        sub.add_option("file", out.texFile, "Root .tex file");
        sub.add_option("--engine", out.engine,
            "TeX engine: pdflatex, xelatex, or lualatex");
        sub.add_option("--engine-program", out.engineProgram,
            "Engine executable to spawn instead of the default");
        sub.add_option("--engine-opt", out.engineOptions,
            "Extra option passed through to the engine verbatim (repeatable)");
        sub.add_option("--tools", out.tools,
            "Comma-separated auxiliary tool roster "
            "(biber,bibtex,makeindex,makeglossaries,pythontex)");
        sub.add_option("-o,--output-dir", out.outputDir,
            "Directory for build outputs and the state database");
        sub.add_flag_callback(
            "--no-halt-on-error", [&out] { out.haltOnError = false; },
            "Keep compiling past TeX errors");
        sub.add_flag_callback(
            "--shell-escape", [&out] { out.shellEscape = true; },
            "Enable \\write18 shell escapes");
        sub.add_flag_callback(
            "--synctex", [&out] { out.synctex = true; },
            "Generate SyncTeX data");
        sub.add_option("--max-passes", out.maxPasses,
               "Give up after this many engine passes")
            ->check(CLI::PositiveNumber);
    }

    void addCommonOptions(CLI::App& sub, CliArgs& out)
    {
        sub.add_flag("-v,--verbose", out.verbose,
            "Show raw log context for each diagnostic");
        sub.add_flag("--plain", out.plain,
            "Plain text output even on a terminal");
        auto* config = sub.add_option("--config", out.configPath,
                              "Config file to use instead of searching for maketex.toml")
                           ->check(CLI::ExistingFile);
        sub.add_flag("--no-config", out.noConfig, "Ignore any maketex.toml")
            ->excludes(config);
    }

} // namespace

ParseResult parseCliArgs(const std::vector<std::string>& args)
{
    CLI::App app { "maketex — a modern latexmk replacement", "maketex" };
    app.set_version_flag("--version", MAKETEX_VERSION);
    app.require_subcommand(1);

    CliArgs out;

    auto* build = app.add_subcommand("build", "Compile the document once (default)");
    addBuildOptions(*build, out);
    addCommonOptions(*build, out);
    // Build-only: watch is interactive, so machine formats make no sense there.
    build->add_option("--format", out.format,
        "Report format: human (default), json, or github");

    auto* watch = app.add_subcommand("watch", "Rebuild whenever a source file changes");
    addBuildOptions(*watch, out);
    addCommonOptions(*watch, out);
    watch->add_option("--interval-ms", out.intervalMs, "File polling interval")
        ->check(CLI::PositiveNumber);
    watch->add_flag("--no-tui", out.noTui,
        "Per-build reports on stdout instead of the full-screen interface");

    auto* clean = app.add_subcommand("clean", "Remove generated files");
    clean->add_option("file", out.texFile, "Root .tex file whose outputs to clean");
    clean->add_option("-o,--output-dir", out.outputDir,
        "Directory the outputs were built into");
    addCommonOptions(*clean, out);
    clean->add_flag("--all", out.cleanAll,
        "Also remove the PDF, SyncTeX data, and the state database");
    clean->add_flag("-n,--dry-run", out.dryRun,
        "Print what would be removed without deleting anything");

    try {
        // CLI11's vector overload consumes arguments from the back.
        std::vector<std::string> reversed(args.rbegin(), args.rend());
        app.parse(std::move(reversed));
    } catch (const CLI::ParseError& e) {
        return { .args = std::nullopt, .exitCode = app.exit(e) };
    }

    if (app.got_subcommand(watch))
        out.command = Command::Watch;
    else if (app.got_subcommand(clean))
        out.command = Command::Clean;
    else
        out.command = Command::Build;

    return { .args = std::move(out), .exitCode = 0 };
}

} // namespace maketex::cli
