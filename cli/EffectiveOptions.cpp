#include "EffectiveOptions.h"

#include "maketex/DocumentProbe.h"

#include <algorithm>

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
                    "unknown tool: {} (expected biber, bib2gls, bibtex, makeindex, "
                    "nomencl, makeglossaries, or pythontex)",
                    name));
            kinds.push_back(*kind);
        }
        return kinds;
    }

    // The document to build: the positional argument (".tex" optional),
    // else the config's `main` (already absolute), else the one document in
    // cwd. Then "% !TEX root" is followed, so building from a chapter file —
    // what editors do — builds the document it belongs to.
    std::expected<fs::path, std::string> resolveTexFile(const CliArgs& args,
        const std::optional<Config>& config, const fs::path& cwd)
    {
        fs::path texFile;
        if (args.texFile) {
            texFile = fs::absolute(*args.texFile);
            if (!fs::is_regular_file(texFile) && texFile.extension() != ".tex") {
                auto withExtension = texFile;
                withExtension += ".tex";
                if (fs::is_regular_file(withExtension))
                    texFile = std::move(withExtension);
            }
        } else if (config && config->main) {
            texFile = *config->main;
        } else {
            auto roots = findRootDocuments(cwd);
            if (roots.empty())
                return std::unexpected(std::format(
                    "no LaTeX document in {}: no .tex file there starts with "
                    "\\documentclass (pass a file, or set `main` in maketex.toml)",
                    cwd.string()));
            if (roots.size() == 1) {
                texFile = roots.front();
            } else {
                // Conventional names break the tie.
                auto dirStem = cwd.filename().string() + ".tex";
                for (const auto& name : { std::string("main.tex"), dirStem }) {
                    auto it = std::ranges::find_if(roots,
                        [&](const fs::path& p) { return p.filename() == name; });
                    if (it != roots.end()) {
                        texFile = *it;
                        break;
                    }
                }
                if (texFile.empty()) {
                    std::string names;
                    for (const auto& r : roots)
                        names += (names.empty() ? "" : ", ") + r.filename().string();
                    return std::unexpected(std::format(
                        "several documents in {}: {} — pass one (e.g. `maketex {}`) "
                        "or set `main` in maketex.toml",
                        cwd.string(), names, roots.front().filename().string()));
                }
            }
        }
        if (!fs::is_regular_file(texFile))
            return std::unexpected(std::format("file not found: {}", texFile.string()));
        return resolveRootDocument(texFile);
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

    auto texFile = resolveTexFile(args, config, fs::current_path());
    if (texFile)
        build.texFile = std::move(*texFile);
    else if (args.command != Command::Doctor || args.texFile)
        // doctor checks the system even without a document; a document it
        // was explicitly given must exist, though.
        return std::unexpected(texFile.error());

    if (args.jobName) {
        if (args.jobName->empty())
            return std::unexpected("jobname must not be empty");
        build.jobName = *args.jobName;
    } else if (config && config->jobName) {
        build.jobName = *config->jobName;
    }

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
    if (auto timeout = args.timeoutSeconds ? args.timeoutSeconds
                                           : (config ? config->timeoutSeconds : std::nullopt))
        build.processTimeout = std::chrono::seconds(*timeout);

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
        build.customTools = config->customTools;
        build.searchPaths = config->searchPaths;
        build.extraEnv = config->extraEnv;
        build.contentFilters = config->contentFilters;
        build.diagnosticPolicy.failOn = config->failOn;
        build.diagnosticPolicy.ignore = config->ignoreWarnings;
        out.extras.watchExtraPaths = config->watchExtraPaths;
        out.extras.cleanExtraExtensions = config->cleanExtraExtensions;
    }
    merge(build.diagnosticPolicy.warningsAsErrors, args.warningsAsErrors,
        config ? config->warningsAsErrors : std::nullopt);
    out.extras.cleanAll = args.cleanAll;
    out.extras.dryRun = args.dryRun;
    out.extras.noTui = args.noTui;
    merge(out.extras.installMissing, args.installMissing,
        config ? config->installMissing : std::nullopt);
    out.extras.installMissingRequested = out.extras.installMissing
        && (args.installMissing || (config && config->installMissing));

    // The engine refuses to write into a missing -output-directory, and the
    // state database needs it to exist too. Clean must not create anything.
    if ((out.command == Command::Build || out.command == Command::Watch)
        && !build.outputDir.empty()) {
        std::error_code ec;
        fs::create_directories(build.outputDir, ec);
        if (ec)
            return std::unexpected(std::format("cannot create output dir {}: {}",
                build.outputDir.string(), ec.message()));
    }

    return out;
}

} // namespace maketex::cli
