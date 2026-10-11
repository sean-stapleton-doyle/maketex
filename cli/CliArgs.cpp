#include "CliArgs.h"

#include "maketex/Build.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <string_view>

namespace maketex::cli {

std::vector<std::string> normalizeArgv(std::vector<std::string> args)
{
    if (args.empty())
        return { "build" }; // the document is found in cwd or the config

    static constexpr std::string_view kSubcommands[] = { "build", "watch", "clean", "doctor" };
    auto isSubcommand = [](std::string_view arg) {
        return std::ranges::find(kSubcommands, arg) != std::ranges::end(kSubcommands);
    };

    const std::string& first = args.front();
    if (first == "help")
        return { "--help" };
    if (isSubcommand(first) || first == "--help" || first == "-h" || first == "--version")
        return args;
    // Flags before the subcommand (`maketex --plain watch`): move it to the
    // front. Only the first positional word counts — any later word is the
    // document, so `maketex paper watch` stays a build of paper — and an
    // option's value is not a positional word (`-o build` names a
    // directory, not the build command).
    static constexpr std::string_view kTakesValue[] = { "-o", "--output-dir", "--engine",
        "--engine-program", "--engine-opt", "--tools", "--max-passes", "--timeout", "--format",
        "--config", "--interval-ms" };
    auto firstNonFlag = args.end();
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (!it->starts_with('-')) {
            firstNonFlag = it;
            break;
        }
        if (std::ranges::find(kTakesValue, *it) != std::ranges::end(kTakesValue) && it + 1 != args.end())
            ++it; // skip the value
    }
    if (firstNonFlag != args.end() && isSubcommand(*firstNonFlag)) {
        auto sub = std::move(*firstNonFlag);
        args.erase(firstNonFlag);
        args.insert(args.begin(), std::move(sub));
        return args;
    }
    args.insert(args.begin(), "build");
    return args;
}

namespace {

    void addInstallMissing(CLI::App& sub, CliArgs& out)
    {
        sub.add_flag_callback(
            "--install-missing", [&out] { out.installMissing = true; },
            "Install missing packages and tools with tlmgr, then rebuild (default; "
            "asks before using sudo unless given explicitly)");
        sub.add_flag_callback(
            "--no-install-missing", [&out] { out.installMissing = false; },
            "Only suggest the install command");
    }

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
        // Every boolean has both spellings so a flag can undo the config.
        sub.add_flag_callback(
            "--halt-on-error", [&out] { out.haltOnError = true; },
            "Stop at the first TeX error (default)");
        sub.add_flag_callback(
            "--no-halt-on-error", [&out] { out.haltOnError = false; },
            "Keep compiling past TeX errors");
        sub.add_flag_callback(
            "--shell-escape", [&out] { out.shellEscape = true; },
            "Enable \\write18 shell escapes (minted, svg, ...)");
        sub.add_flag_callback(
            "--no-shell-escape", [&out] { out.shellEscape = false; },
            "Disable shell escapes (default)");
        sub.add_flag_callback(
            "--synctex", [&out] { out.synctex = true; },
            "Generate SyncTeX data (default)");
        sub.add_flag_callback(
            "--no-synctex", [&out] { out.synctex = false; },
            "Skip SyncTeX data");
        sub.add_option("--max-passes", out.maxPasses,
               "Give up after this many engine passes")
            ->check(CLI::PositiveNumber);
        addInstallMissing(sub, out);
        sub.add_option("--timeout", out.timeoutSeconds,
               "Kill any engine or tool run that takes longer than this many seconds")
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
    app.set_version_flag("--version", std::string(maketex::version()));
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

    auto* doctor = app.add_subcommand("doctor",
        "Check that the engines, tools, and packages a build needs are installed");
    doctor->add_option("file", out.texFile, "Document whose requirements to check too");
    addCommonOptions(*doctor, out);
    addInstallMissing(*doctor, out);

    try {
        // CLI11's vector overload consumes arguments from the back.
        std::vector<std::string> reversed(args.rbegin(), args.rend());
        app.parse(std::move(reversed));
    } catch (const CLI::ParseError& e) {
        // --help/--version exit 0; every usage error exits 2 (CLI11's own
        // codes run past 100 and mean nothing to callers).
        int code = app.exit(e);
        return { .args = std::nullopt, .exitCode = code == 0 ? 0 : kExitUsage };
    }

    if (app.got_subcommand(watch))
        out.command = Command::Watch;
    else if (app.got_subcommand(clean))
        out.command = Command::Clean;
    else if (app.got_subcommand(doctor))
        out.command = Command::Doctor;
    else
        out.command = Command::Build;

    return { .args = std::move(out), .exitCode = 0 };
}

} // namespace maketex::cli
