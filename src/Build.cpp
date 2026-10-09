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
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace maketex {

std::vector<fs::path> texSystemRoots()
{
    // Constant for the life of the process, and a watch session builds many
    // times: ask kpsewhich once, for every tree in one call.
    static const std::vector<fs::path> roots = [] {
        // The distribution and its generated caches. TEXMFHOME is left out
        // on purpose: it holds the user's own packages, whose edits must
        // trigger rebuilds. TEXMFLOCAL sits outside TEXMFROOT on a full
        // MacTeX/TeX Live install, and Debian splits out TEXMFMAIN.
        constexpr std::string_view kVars[] = { "TEXMFROOT", "TEXMFDIST", "TEXMFMAIN",
            "TEXMFLOCAL", "TEXMFSYSVAR", "TEXMFSYSCONFIG", "TEXMFVAR", "TEXMFCONFIG" };
        std::string query = "-expand-var=";
        for (auto var : kVars) {
            query += '$';
            query += var;
            query += '|';
        }
        std::vector<fs::path> found;
        try {
            std::vector<std::string> args { "kpsewhich", query };
            auto result = Process(args).wait();
            if (result.exitCode != 0)
                return found;
            std::string_view rest = result.stdoutOutput;
            while (!rest.empty()) {
                auto bar = rest.find('|');
                auto value = rest.substr(0, bar);
                while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
                    value.remove_suffix(1);
                if (value.starts_with("!!"))
                    value.remove_prefix(2);
                // Unset variables expand to nothing or stay "$VAR"; path
                // lists ({a,b}, a:b) are not single roots.
                if (!value.empty() && !value.starts_with('$')
                    && value.find_first_of("{}:;") == std::string_view::npos)
                    found.emplace_back(value);
                if (bar == std::string_view::npos)
                    break;
                rest.remove_prefix(bar + 1);
            }
        } catch (const std::system_error&) {
            // kpsewhich not installed or not executable: nothing to ignore.
        }
        return found;
    }();
    return roots;
}

namespace {

    // Splits a user-supplied -jobname out of the engine's extra options:
    // the job name decides where the .fls, .log, .aux, and tool files are,
    // so it must reach EngineInvocation rather than pass through verbatim.
    // Accepts -jobname=x, --jobname=x, and -jobname x.
    std::pair<EngineArguments, std::optional<std::string>> splitJobName(
        EngineArguments arguments)
    {
        std::optional<std::string> jobName;
        std::vector<std::string> kept;
        auto& opts = arguments.extraOptions;
        for (size_t i = 0; i < opts.size(); ++i) {
            std::string_view opt = opts[i];
            if (opt.starts_with("--"))
                opt.remove_prefix(1);
            if (opt.starts_with("-jobname=")) {
                jobName = std::string(opt.substr(9));
            } else if (opt == "-jobname" && i + 1 < opts.size()) {
                jobName = opts[++i];
            } else {
                kept.push_back(opts[i]);
            }
        }
        opts = std::move(kept);
        if (jobName && jobName->empty())
            jobName.reset();
        return { std::move(arguments), std::move(jobName) };
    }

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
    auto [engineArguments, jobName] = splitJobName(options.engineArguments);
    auto engine = createEngine(options.engine, engineArguments);

    fs::path outDir = options.resolvedOutputDir();
    fs::create_directories(outDir);
    fs::path dbPath = outDir / ".maketex.db";

    Blake3Hasher hasher;

    std::vector<std::unique_ptr<ITool>> tools;
    tools.reserve(options.tools.size());
    for (ToolKind kind : options.tools)
        tools.push_back(createTool(kind));

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
        .jobName = jobName,
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
    EngineInvocation inv {
        .texFile = options.texFile,
        .outputDir = options.resolvedOutputDir(),
        .jobName = splitJobName(options.engineArguments).second,
    };
    fs::path flsPath = inv.jobFile(".fls");

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
