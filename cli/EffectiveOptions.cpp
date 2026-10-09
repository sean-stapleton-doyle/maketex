#include "EffectiveOptions.h"

#include <format>
#include <ranges>
#include <string_view>

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    std::expected<std::vector<ToolKind>, std::string> parseToolList(
        std::string_view list)
    {
        std::vector<ToolKind> kinds;
        for (auto part : std::views::split(list, ',')) {
            std::string_view name(part);
            if (name.empty())
                continue;
            auto kind = parseToolKind(name);
            if (!kind)
                return std::unexpected(std::format(
                    "unknown tool: {} (expected biber, bibtex, makeindex, "
                    "makeglossaries, or pythontex)",
                    name));
            kinds.push_back(*kind);
        }
        return kinds;
    }

    // flag > config > existing default in `target`.
    template <typename T, typename U>
    void merge(T& target, const std::optional<U>& flag,
        const std::optional<U>& fromConfig)
    {
        if (flag)
            target = *flag;
        else if (fromConfig)
            target = *fromConfig;
    }

} // namespace

std::expected<EffectiveOptions, std::string> assembleOptions(
    const CliArgs& args, const std::optional<Config>& config)
{
    EffectiveOptions out;
    out.command = args.command;
    BuildOptions& build = out.build;

    // The positional argument beats the config's `main`. Config paths are
    // already absolute (resolved against the config file's directory).
    if (args.texFile)
        build.texFile = fs::absolute(*args.texFile);
    else if (config && config->main)
        build.texFile = *config->main;
    else
        return std::unexpected(
            "no input file: pass a .tex file or set `main` in maketex.toml");

    if (!fs::is_regular_file(build.texFile))
        return std::unexpected(
            std::format("file not found: {}", build.texFile.string()));

    if (args.engine) {
        auto kind = parseEngineKind(*args.engine);
        if (!kind)
            return std::unexpected(std::format(
                "unknown engine: {} (expected pdflatex, xelatex, or lualatex)",
                *args.engine));
        build.engine = *kind;
    } else if (config && config->engine) {
        build.engine = *config->engine;
    }

    merge(build.engineArguments.program, args.engineProgram,
        config ? config->engineProgram : std::nullopt);
    if (!args.engineOptions.empty())
        build.engineArguments.extraOptions = args.engineOptions;
    else if (config)
        build.engineArguments.extraOptions = config->engineOptions;

    if (args.tools) {
        auto kinds = parseToolList(*args.tools);
        if (!kinds)
            return std::unexpected(kinds.error());
        build.tools = std::move(*kinds);
    } else if (config && config->tools) {
        build.tools = *config->tools;
    }

    if (args.outputDir)
        build.outputDir = fs::absolute(*args.outputDir);
    else if (config && config->outputDir)
        build.outputDir = *config->outputDir;

    merge(build.haltOnError, args.haltOnError,
        config ? config->haltOnError : std::nullopt);
    merge(build.shellEscape, args.shellEscape,
        config ? config->shellEscape : std::nullopt);
    merge(build.synctex, args.synctex, config ? config->synctex : std::nullopt);
    merge(build.maxPasses, args.maxPasses,
        config ? config->maxPasses : std::nullopt);
    if (build.maxPasses <= 0)
        return std::unexpected("max-passes must be positive");

    out.extras.verbose = args.verbose;
    out.extras.plain = args.plain;
    if (args.format) {
        if (*args.format == "human")
            out.extras.format = OutputFormat::Human;
        else if (*args.format == "json")
            out.extras.format = OutputFormat::Json;
        else if (*args.format == "github")
            out.extras.format = OutputFormat::Github;
        else
            return std::unexpected(std::format(
                "unknown format: {} (expected human, json, or github)",
                *args.format));
    }
    merge(out.extras.watchIntervalMs, args.intervalMs,
        config ? config->watchIntervalMs : std::nullopt);
    if (out.extras.watchIntervalMs <= 0)
        return std::unexpected("watch interval must be positive");
    if (config) {
        out.extras.watchExtraPaths = config->watchExtraPaths;
        out.extras.cleanExtraExtensions = config->cleanExtraExtensions;
    }
    out.extras.cleanAll = args.cleanAll;
    out.extras.dryRun = args.dryRun;
    out.extras.noTui = args.noTui;

    // The engine refuses to write into a missing -output-directory, and the
    // state database needs it to exist too. Clean must not create anything.
    if (out.command != Command::Clean && !build.outputDir.empty()) {
        std::error_code ec;
        fs::create_directories(build.outputDir, ec);
        if (ec)
            return std::unexpected(std::format("cannot create output dir {}: {}",
                build.outputDir.string(), ec.message()));
    }

    return out;
}

} // namespace maketex::cli
