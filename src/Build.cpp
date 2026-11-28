#include "maketex/Build.h"

#include "maketex/Blake3Hasher.h"
#include "maketex/DocumentProbe.h"
#include "maketex/EngineInvocation.h"
#include "maketex/ITool.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolFactory.h"
#include "maketex/build/BuildAttempts.h"
#include "maketex/build/BuildHeuristics.h"
#include "maketex/build/BuildLock.h"
#include "maketex/build/EngineMemory.h"
#include "maketex/build/Environment.h"
#include "maketex/build/Fingerprint.h"
#include "maketex/build/JobName.h"
#include "maketex/build/OutputDir.h"
#include "maketex/build/PreambleFormat.h"
#include "maketex/build/PreambleHooks.h"
#include "maketex/build/Publish.h"
#include "maketex/build/TexSystemRoots.h"
#include "maketex/engines/EngineFactory.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

#ifndef MAKETEX_VERSION
#define MAKETEX_VERSION "0.0.0-unknown"
#endif

namespace maketex {

std::string_view version()
{
    return MAKETEX_VERSION;
}

namespace {

    fs::path prepareOutputDir(const BuildOptions& options)
    {
        fs::path outDir = options.resolvedOutputDir();
        fs::create_directories(outDir);
        if (options.usesDefaultOutputDir())
            build::ignoreInVersionControl(outDir);
        build::mirrorSourceDirectories(options.texFile.parent_path(), outDir);
        return outDir;
    }

    std::vector<std::unique_ptr<ITool>> createTools(const BuildOptions& options)
    {
        std::vector<std::unique_ptr<ITool>> tools;
        tools.reserve(options.tools.size() + options.customTools.size());
        for (ToolKind kind : options.tools)
            tools.push_back(createTool(kind));
        for (const auto& recipe : options.customTools)
            tools.push_back(std::make_unique<HashTriggeredTool>(recipe));
        return tools;
    }

    EngineInvocation makeEngineInvocation(const BuildOptions& options,
        const fs::path& outDir,
        const std::optional<std::string>& flagJobName,
        std::vector<std::pair<std::string, std::string>> extraEnv)
    {
        return EngineInvocation {
            .texFile = options.texFile,
            .outputDir = outDir,
            .jobName = options.jobName ? options.jobName : flagJobName,
            .haltOnError = options.haltOnError,
            .shellEscape = options.shellEscape,
            .synctex = options.synctex,
            .extraEnv = std::move(extraEnv),
        };
    }

    void maybeAttachPreambleFormat(const BuildOptions& options,
        EngineKind engineKind,
        const EngineArguments& engineArguments,
        const fs::path& outDir,
        const std::vector<std::pair<std::string, std::string>>& extraEnv,
        StateDatabase& stateDb,
        Blake3Hasher& hasher,
        EngineInvocation& inv)
    {
        if (!options.preambleCache || engineKind != EngineKind::PdfLaTeX)
            return;
        auto fmt = build::preparePreambleFormat(options, engineArguments, outDir,
            inv.resolvedJobName(), extraEnv, stateDb, hasher);
        if (fmt.empty())
            return;
        inv.formatName = std::move(fmt);
        inv.skipPreamble = true;
        inv.extraEnv.emplace_back("TEXFORMATS", build::prependTexFormats(outDir));
    }

    BuildDriver::Config makeDriverConfig(const BuildOptions& options,
        EngineKind engineKind,
        const EngineArguments& engineArguments,
        const fs::path& outDir,
        const EngineInvocation& inv,
        std::vector<ITool*> toolPtrs)
    {
        const bool xdvAvailable = engineKind == EngineKind::XeLaTeX
            && build::findOnPath("xdvipdfmx").has_value();
        const bool coldMultiPass = !xdvAvailable
            && (engineKind == EngineKind::PdfLaTeX || engineKind == EngineKind::LuaLaTeX)
            && options.maxPasses >= 2
            && build::likelyMultiPassColdBuild(options.texFile, inv.jobFile(".aux"));
        const auto modes = build::chooseDriverModes(
            engineKind, options.maxPasses, xdvAvailable, coldMultiPass);

        BuildDriver::Config driverConfig {
            .maxPasses = options.maxPasses,
            .tools = std::move(toolPtrs),
            .ignorePrefixes = texSystemRoots(),
            .observer = options.observer,
            .processLimits = WaitLimits {
                .timeout = options.processTimeout,
                .stop = options.stopToken,
            },
            .diagnosticPolicy = options.diagnosticPolicy,
            .preambleProvider = [texFile = options.texFile, outDir, engineKind, prog = engineArguments.program, shellEscape = options.shellEscape] {
                return build::buildPreambleHooks(texFile, outDir, engineKind, prog, shellEscape);
            },
            .draftFirstPass = modes.draftFirstPass,
            .xelatexXdvSplit = modes.xelatexXdvSplit,
        };
        if (!options.contentFilters.empty())
            driverConfig.contentFilters = build::mergeContentFilters(options.contentFilters);
        return driverConfig;
    }

    void finalizeBuildResult(BuildResult& result,
        const BuildOptions& options,
        const fs::path& outDir,
        const EngineInvocation& inv,
        StateDatabase& stateDb,
        bool suppressResetNotice)
    {
        if (stateDb.wasReset() && !suppressResetNotice)
            result.diagnostics.insert(result.diagnostics.begin(),
                Diagnostic {
                    .severity = Diagnostic::Severity::Info,
                    .message = "stored build state was incompatible "
                               "(different maketex version or build options) — "
                               "started from a full rebuild",
                });

        if (result.success && options.usesDefaultOutputDir()) {
            result.published = build::publishResultArtifacts(result.upToDate,
                outDir, options.texFile.parent_path(), inv.resolvedJobName(), stateDb);
        } else if (result.success) {
            result.published.push_back(inv.jobFile(".pdf"));
        }
    }

    BuildResult buildWith(const BuildOptions& options, EngineKind engineKind,
        bool suppressResetNotice)
    {
        auto [engineArguments, flagJobName] = build::splitJobName(options.engineArguments);
        auto engine = createEngine(engineKind, engineArguments);
        fs::path outDir = prepareOutputDir(options);

        Blake3Hasher hasher;
        auto tools = createTools(options);
        auto extraEnv = build::searchPathEnv(options);
        auto inv = makeEngineInvocation(options, outDir, flagJobName, extraEnv);

        // Held until the build returns: a second maketex building this
        // document (watch plus a manual run) waits instead of interleaving
        // writes to the same .aux and PDF.
        build::BuildLock lock(buildLockPath(outDir, inv.resolvedJobName()));

        std::vector<std::string_view> toolNames;
        std::vector<ITool*> toolPtrs;
        toolNames.reserve(tools.size());
        toolPtrs.reserve(tools.size());
        for (const auto& t : tools) {
            toolNames.push_back(t->name());
            toolPtrs.push_back(t.get());
        }

        auto db = std::make_unique<SQLite::Database>(
            (outDir / ".maketex.db").string(),
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        StateDatabase stateDb(std::move(db),
            StateDbConfig {
                .projectRoot = options.texFile.parent_path(),
                .fingerprint = build::buildFingerprint(options, engine->name(), toolNames),
                .jobName = inv.resolvedJobName(),
            });

        maybeAttachPreambleFormat(
            options, engineKind, engineArguments, outDir, extraEnv, stateDb, hasher, inv);

        auto driverConfig = makeDriverConfig(
            options, engineKind, engineArguments, outDir, inv, std::move(toolPtrs));
        BuildDriver driver(*engine, stateDb, hasher, inv, std::move(driverConfig));
        BuildResult result;
        static_cast<DriverResult&>(result) = driver.run();
        result.engine = engineKind;

        finalizeBuildResult(result, options, outDir, inv, stateDb, suppressResetNotice);
        return result;
    }

    class SystemBuildAttempts final : public build::IBuildAttempts {
    public:
        EngineChoice chooseEngine(const BuildOptions& options) override
        {
            return maketex::chooseEngine(options);
        }

        BuildResult attempt(const BuildOptions& options,
            EngineKind engine,
            bool suppressResetNotice) override
        {
            return buildWith(options, engine, suppressResetNotice);
        }

        std::optional<EngineKind> rememberedEngine(const BuildOptions& options) override
        {
            return build::loadDetectedEngine(options);
        }

        void rememberEngine(const BuildOptions& options, EngineKind engine) override
        {
            build::saveDetectedEngine(options, engine);
        }

        void discardPartialAux(const BuildOptions& options) override
        {
            std::error_code ec;
            fs::remove(options.resolvedOutputDir() / (jobNameFor(options) + ".aux"), ec);
        }
    };

} // namespace

std::string jobNameFor(const BuildOptions& options)
{
    if (options.jobName && !options.jobName->empty())
        return *options.jobName;
    return build::splitJobName(options.engineArguments)
        .second.value_or(options.texFile.stem().string());
}

std::vector<fs::path> publishedArtifactPaths(const BuildOptions& options)
{
    if (!options.usesDefaultOutputDir())
        return { };
    auto texDir = options.texFile.parent_path();
    auto job = jobNameFor(options);
    return { texDir / (job + ".pdf"), texDir / (job + ".synctex.gz") };
}

EngineChoice chooseEngine(const BuildOptions& options)
{
    if (options.engine)
        return { .kind = *options.engine, .reason = "" };
    return detectEngine(options.texFile);
}

BuildResult runBuild(const BuildOptions& options)
{
    SystemBuildAttempts attempts;
    return build::runBuild(options, attempts);
}

} // namespace maketex
