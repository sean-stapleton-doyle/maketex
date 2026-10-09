#include "maketex/Build.h"

#include "maketex/Blake3Hasher.h"
#include "maketex/EngineInvocation.h"
#include "maketex/ITool.h"
#include "maketex/Process.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolFactory.h"
#include "maketex/engines/EngineFactory.h"
#include "maketex/parsers/FlsParser.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace maketex {

std::vector<fs::path> texSystemRoots()
{
    std::vector<fs::path> roots;
    for (const char* var : { "TEXMFROOT", "TEXMFSYSVAR", "TEXMFVAR" }) {
        try {
            std::vector<std::string> args { "kpsewhich", "-var-value", var };
            auto result = Process(args).wait();
            if (result.exitCode != 0)
                continue;
            auto value = result.stdoutOutput;
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
                value.pop_back();
            if (!value.empty())
                roots.emplace_back(value);
        } catch (const std::system_error& e) {
            // kpsewhich not installed (ENOENT) or not executable: nothing to
            // ignore. Any other system_error from the first invocation will
            // recur on the rest, so break out rather than retry.
            (void)e;
            break;
        }
    }
    return roots;
}

namespace {

    // A readable summary of everything that changes what the engine and
    // tools produce. A stored database whose fingerprint differs was built
    // under different semantics, so its hashes must not drive incremental
    // decisions. Deliberately not hashed — debuggability beats brevity.
    std::string buildFingerprint(const BuildOptions& options,
        std::string_view engineName,
        const std::vector<std::unique_ptr<ITool>>& tools)
    {
        std::string fp = "engine=";
        fp += engineName;
        fp += ";program=";
        fp += options.engineArguments.program;
        fp += ";opts=";
        for (const auto& opt : options.engineArguments.extraOptions) {
            fp += opt;
            fp += ',';
        }
        fp += ";tools=";
        for (const auto& tool : tools) {
            fp += tool->name();
            fp += ',';
        }
        fp += ";shellEscape=";
        fp += options.shellEscape ? '1' : '0';
        fp += ";synctex=";
        fp += options.synctex ? '1' : '0';
        fp += ";haltOnError=";
        fp += options.haltOnError ? '1' : '0';
        return fp;
    }

} // namespace

DriverResult runBuild(const BuildOptions& options)
{
    auto engine = createEngine(options.engine, options.engineArguments);

    fs::path outDir = options.resolvedOutputDir();
    fs::create_directories(outDir);
    fs::path dbPath = outDir / ".maketex.db";

    Blake3Hasher hasher;

    ToolFactory factory(hasher);
    std::vector<std::unique_ptr<ITool>> tools;
    tools.reserve(options.tools.size());
    for (ToolKind kind : options.tools)
        tools.push_back(factory.create(kind));

    auto db = std::make_unique<SQLite::Database>(
        dbPath.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    StateDatabase stateDb(std::move(db),
        StateDbConfig {
            .projectRoot = options.texFile.parent_path(),
            .fingerprint = buildFingerprint(options, engine->name(), tools),
        });

    std::vector<ITool*> toolPtrs;
    toolPtrs.reserve(tools.size());
    for (auto& t : tools)
        toolPtrs.push_back(t.get());

    EngineInvocation inv {
        .texFile = options.texFile,
        .outputDir = outDir,
        .haltOnError = options.haltOnError,
        .shellEscape = options.shellEscape,
        .synctex = options.synctex,
    };

    BuildDriver driver(*engine, stateDb, hasher, inv,
        BuildDriver::Config {
            .maxPasses = options.maxPasses,
            .tools = std::move(toolPtrs),
            .ignorePrefixes = texSystemRoots(),
            .observer = options.observer,
        });
    auto result = driver.run();

    if (stateDb.wasReset())
        result.diagnostics.insert(result.diagnostics.begin(),
            Diagnostic {
                .severity = Diagnostic::Severity::Info,
                .message = "stored build state was incompatible "
                           "(different maketex version or build options) — "
                           "started from a full rebuild",
            });

    // Unconditional on success — also after an up-to-date skip, which covers
    // a published PDF the user deleted while the build state stayed current.
    if (result.success && options.usesDefaultOutputDir())
        publishArtifacts(outDir, options.texFile.parent_path(),
            inv.resolvedJobName());

    return result;
}

std::vector<fs::path> publishArtifacts(const fs::path& outDir,
    const fs::path& destDir, const std::string& jobName)
{
    std::vector<fs::path> published;
    std::error_code ec;
    if (fs::equivalent(outDir, destDir, ec))
        return published;

    for (const char* ext : { ".pdf", ".synctex.gz" }) {
        fs::path src = outDir / (jobName + ext);
        if (!fs::is_regular_file(src, ec))
            continue;
        fs::path dest = destDir / (jobName + ext);
        // Copy to a sibling temp file first: rename within destDir is atomic
        // on POSIX, so a viewer refreshing mid-publish sees old or new,
        // never a torn file. The temp name is fixed — concurrent builds of
        // the same document already race on the outputs themselves.
        fs::path tmp = dest;
        tmp += ".maketex-tmp";
        // copy_file returns bool; rename (ec overload) returns void, so check
        // ec explicitly after the call.
        bool copied = fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec);
        if (!ec)
            fs::rename(tmp, dest, ec);
        if (ec || !copied) {
            // Publish failures are non-fatal — the build already succeeded;
            // remove a half-written temp so the next attempt is clean. The
            // missing artifact is reflected by its absence from the returned
            // vector, which the caller treats as best-effort.
            std::error_code removeEc;
            fs::remove(tmp, removeEc);
            continue;
        }
        published.push_back(std::move(dest));
    }
    return published;
}

std::optional<BuildFileSets> scanBuildFiles(const BuildOptions& options)
{
    fs::path outDir = options.resolvedOutputDir();
    fs::path flsPath = outDir / (options.texFile.stem().string() + ".fls");

    FlsParser parser;
    // Re-root recorded paths in case the project moved since the last build
    // (see BuildDriver::updateGraphFromFls).
    parser.overridePwd(options.texFile.parent_path());
    for (const auto& root : texSystemRoots())
        parser.addIgnorePrefix(root);
    auto record = parser.parse(flsPath);
    if (!record)
        return std::nullopt;

    std::unordered_set<std::string> outputPaths;
    for (const auto& p : record->outputs)
        outputPaths.insert(p.string());

    BuildFileSets sets;
    for (const auto& p : record->inputs) {
        if (!outputPaths.contains(p.string()))
            sets.sources.push_back(p);
    }
    sets.generated = std::move(record->outputs);
    return sets;
}

} // namespace maketex
