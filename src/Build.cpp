#include "maketex/Build.h"

#include "maketex/Blake3Hasher.h"
#include "maketex/DocumentProbe.h"
#include "maketex/EngineInvocation.h"
#include "maketex/ITool.h"
#include "maketex/Process.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolContext.h"
#include "maketex/ToolFactory.h"
#include "maketex/engines/EngineFactory.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"
#include "maketex/tools/InputLocator.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace maketex {

std::string_view version()
{
    return MAKETEX_VERSION;
}

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

    // Joins configured kpathsea search directories with ':' and appends any
    // inherited value (or a trailing ':' so TeX Live's built-in defaults stay
    // on the search path).
    std::vector<std::pair<std::string, std::string>> resolvedEnv(
        const BuildOptions& options)
    {
        std::vector<std::pair<std::string, std::string>> env = options.extraEnv;
        auto addSearchPath = [&](const char* var, const std::vector<std::string>& dirs) {
            if (dirs.empty())
                return;
            std::string value;
            for (const auto& d : dirs) {
                value += d;
                value += ':';
            }
            auto existingIt = std::ranges::find(env, std::string_view(var),
                &std::pair<std::string, std::string>::first);
            if (existingIt != env.end()) {
                value += existingIt->second;
                existingIt->second = std::move(value);
                return;
            }
            if (const char* inherited = std::getenv(var); inherited && *inherited)
                value += inherited;
            env.emplace_back(var, std::move(value));
        };
        addSearchPath("TEXINPUTS", options.searchPaths.texInputs);
        addSearchPath("BIBINPUTS", options.searchPaths.bibInputs);
        addSearchPath("BSTINPUTS", options.searchPaths.bstInputs);
        addSearchPath("INDEXSTYLE", options.searchPaths.indexStyle);
        return env;
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
        for (const auto& c : options.customTools) {
            fp += ";custom=";
            fp += c.name;
            fp += ':';
            for (const auto& a : c.argv) {
                fp += a;
                fp += ' ';
            }
            for (const auto& t : c.triggerExtensions) {
                fp += t;
                fp += ',';
            }
            for (const auto& o : c.outputExtensions) {
                fp += o;
                fp += ',';
            }
            fp += c.workingDir == ToolWorkingDir::Source ? 'S' : 'O';
        }
        fp += ";shellEscape=";
        fp += options.shellEscape ? '1' : '0';
        fp += ";synctex=";
        fp += options.synctex ? '1' : '0';
        fp += ";haltOnError=";
        fp += options.haltOnError ? '1' : '0';
        fp += ";werror=";
        fp += options.diagnosticPolicy.warningsAsErrors ? '1' : '0';
        for (const auto& f : options.diagnosticPolicy.failOn) {
            fp += ";failOn=";
            fp += f;
        }
        for (const auto& ig : options.diagnosticPolicy.ignore) {
            fp += ";ignore=";
            fp += ig;
        }
        for (const auto& [ext, pat] : options.contentFilters) {
            fp += ";filter=";
            fp += ext;
            fp += ':';
            fp += pat;
        }
        for (const auto& [k, v] : options.extraEnv) {
            fp += ";env:";
            fp += k;
            fp += '=';
            fp += v;
        }
        auto appendPaths = [&](std::string_view tag, const std::vector<std::string>& ps) {
            if (ps.empty())
                return;
            fp += ';';
            fp += tag;
            fp += '=';
            for (const auto& p : ps) {
                fp += p;
                fp += ':';
            }
        };
        appendPaths("cfgTEXINPUTS", options.searchPaths.texInputs);
        appendPaths("cfgBIBINPUTS", options.searchPaths.bibInputs);
        appendPaths("cfgBSTINPUTS", options.searchPaths.bstInputs);
        appendPaths("cfgINDEXSTYLE", options.searchPaths.indexStyle);
        // Where the engine and tools look for inputs changes what they
        // produce as surely as a flag does. The user's own values only: the
        // document directory maketex adds must stay out, or moving the
        // project (a restored CI cache) would reset its state.
        for (const char* var : { "TEXINPUTS", "BIBINPUTS", "BSTINPUTS", "INDEXSTYLE" }) {
            fp += ';';
            fp += var;
            fp += '=';
            if (const char* value = std::getenv(var))
                fp += value;
        }
        return fp;
    }

} // namespace

namespace {

    // An exclusive flock(2) on a lock file, released on destruction. Lock
    // trouble (an unwritable directory) is not fatal: the build proceeds
    // unlocked, as before locks existed.
    class BuildLock {
    public:
        explicit BuildLock(const fs::path& path)
            : m_fd(::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644))
        {
            if (m_fd >= 0)
                while (::flock(m_fd, LOCK_EX) != 0 && errno == EINTR) { }
        }
        ~BuildLock()
        {
            if (m_fd >= 0)
                ::close(m_fd); // releases the lock
        }
        BuildLock(const BuildLock&) = delete;
        BuildLock& operator=(const BuildLock&) = delete;

    private:
        int m_fd;
    };

    bool shouldMirrorForFile(const fs::path& p)
    {
        static constexpr std::array<std::string_view, 11> kExts = {
            ".tex", ".eps", ".ps", ".svg", ".pdf", ".png", ".jpg", ".jpeg",
            ".tikz", ".pgf", ".bib"
        };
        auto ext = p.extension().string();
        return std::ranges::find(kExts, ext) != kExts.end();
    }

    // \include{chapters/one} makes the engine write chapters/one.aux under
    // -output-directory, and tools/packages like epstopdf, svg, and TikZ
    // externalize write converted assets into figure subdirectories. TeX
    // cannot create directories, so mirror source subdirectories that hold
    // TeX/graphics assets into the output directory.
    void mirrorSourceDirectories(const fs::path& texDir, const fs::path& outDir)
    {
        std::error_code ec;
        if (fs::equivalent(texDir, outDir, ec))
            return;
        auto outCanonical = fs::weakly_canonical(outDir, ec);
        // Bounded: running in a huge directory (a home dir) must stay cheap.
        constexpr int kMaxEntries = 20000;
        int seen = 0;
        fs::recursive_directory_iterator it(texDir,
            fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (++seen > kMaxEntries)
                break;
            const auto& entry = *it;
            auto name = entry.path().filename().string();
            if (entry.is_directory(ec)) {
                // Hidden dirs (.git, .maketex), the output dir, and
                // dependency trees never hold document inputs.
                if (name.starts_with('.') || name == "node_modules" || name == "__pycache__"
                    || fs::weakly_canonical(entry.path(), ec) == outCanonical)
                    it.disable_recursion_pending();
                continue;
            }
            if (!shouldMirrorForFile(entry.path()))
                continue;
            auto rel = fs::relative(entry.path().parent_path(), texDir, ec);
            if (!ec && !rel.empty() && rel != ".")
                fs::create_directories(outDir / rel, ec);
        }
    }

    // The default output directory is maketex's own scratch space; keep it
    // out of version control without the user having to say so.
    void ignoreInVersionControl(const fs::path& outDir)
    {
        auto gitignore = outDir / ".gitignore";
        std::error_code ec;
        if (!fs::exists(gitignore, ec))
            std::ofstream(gitignore) << "# Created by maketex: build outputs and state.\n*\n";
    }

    // Whether a failed pdflatex build failed because the document needs a
    // Unicode engine (fontspec, unicode-math, luacode, graphdrawing, … loaded
    // where the preamble scan could not see them, e.g. from a custom class).
    std::optional<EngineKind> unicodeEngineNeeded(const DriverResult& result)
    {
        for (const auto& d : result.diagnostics) {
            const auto& m = d.message;
            // Matched on prefixes/phrases; check the "either / LuaTeX" cases
            // before XeTeX-only ones so "requires XeTeX or LuaTeX" selects
            // LuaLaTeX rather than matching "requires XeTeX".
            if (m.contains("requires either XeTeX or") || m.contains("XeLaTeX or LuaLaTeX")
                || m.contains("XeTeX or LuaTeX") || m.contains("Cannot be run with pdfLaTeX")
                || m.contains("requires LuaTeX") || m.contains("LuaTeX is required")
                || m.contains("run LuaTeX") || m.contains("need LuaTeX")
                || m.contains("requires LuaLaTeX") || m.contains("LuaLaTeX is required"))
                return EngineKind::LuaLaTeX;
            if (m.contains("requires XeTeX") || m.contains("XeTeX is required")
                || m.contains("requires XeLaTeX") || m.contains("XeLaTeX is required"))
                return EngineKind::XeLaTeX;
        }
        return std::nullopt;
    }

    std::optional<EngineKind> loadDetectedEngine(const BuildOptions& options)
    {
        fs::path dbPath = options.resolvedOutputDir() / ".maketex.db";
        std::error_code ec;
        if (!fs::is_regular_file(dbPath, ec))
            return std::nullopt;
        try {
            SQLite::Database db(dbPath.string(), SQLite::OPEN_READONLY);
            db.setBusyTimeout(10'000);
            SQLite::Statement q(db, "SELECT value FROM meta WHERE key = ?");
            q.bind(1, "job:" + jobNameFor(options) + ":detected_engine");
            if (!q.executeStep())
                return std::nullopt;
            return parseEngineKind(q.getColumn(0).getString());
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    void saveDetectedEngine(const BuildOptions& options, EngineKind engine)
    {
        fs::path dbPath = options.resolvedOutputDir() / ".maketex.db";
        try {
            auto db = std::make_unique<SQLite::Database>(
                dbPath.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
            StateDatabase stateDb(std::move(db));
            stateDb.setJobMeta(jobNameFor(options), "detected_engine",
                std::string(engineKindName(engine)));
        } catch (const std::exception&) {
            // Best-effort cache; the build already succeeded.
        }
    }

    // minted before v3 runs pygmentize from the document directory but
    // expects its files where the engine writes them, so with an output
    // directory it needs `outputdir` (minted 3, TeX Live 2024+, finds it
    // itself). Similarly, TikZ externalization spawns a child TeX process
    // that does not inherit `-output-directory` unless `external/system call`
    // passes it. Returns any TeX preamble hooks to run before the document.
    std::string buildPreambleHooks(const fs::path& texFile, const fs::path& outDir,
        EngineKind engineKind, std::string_view engineProgram, bool shellEscape)
    {
        std::error_code ec;
        if (fs::equivalent(texFile.parent_path(), outDir, ec))
            return { };

        auto rel = fs::relative(outDir, texFile.parent_path(), ec).generic_string();
        if (ec || rel.empty() || rel == ".")
            return { };

        std::string preamble;
        auto req = analyzeRequirements(texFile);
        if (std::ranges::find(req.packages, "minted") != req.packages.end()) {
            std::string sty;
            try {
                auto r = Process(std::vector<std::string> { "kpsewhich", "minted.sty" })
                             .wait({ .timeout = std::chrono::seconds(5) });
                sty = r.stdoutOutput.substr(0, r.stdoutOutput.find('\n'));
            } catch (const std::system_error&) {
            }
            std::ifstream in(sty);
            std::string line;
            bool v2 = false;
            for (int i = 0; i < 60 && std::getline(in, line); ++i) {
                if (auto v = line.find(" v"); line.contains("Yet another Pygments shim") && v != std::string::npos) {
                    int major = 0;
                    const char* first = line.data() + v + 2;
                    const char* last = line.data() + line.size();
                    auto [ptr, parseEc] = std::from_chars(first, last, major);
                    v2 = parseEc == std::errc { } && major < 3;
                    break;
                }
            }
            if (v2)
                preamble += "\\PassOptionsToPackage{outputdir=" + rel + "}{minted}";
        }

        // TikZ externalization: pass -output-directory into the child engine
        // call when the user hasn't customized `system call` themselves.
        if (shellEscape) {
            std::ifstream in(texFile);
            std::string source((std::istreambuf_iterator<char>(in)), { });
            if ((source.contains("\\tikzexternalize") || source.contains("\\usetikzlibrary{external}"))
                && !source.contains("system call")) {
                std::string bin = engineProgram.empty()
                    ? std::string(engineKindName(engineKind))
                    : std::string(engineProgram);
                preamble += "\\AtBeginDocument{\\ifdefined\\tikzset\\tikzset{external/system call={"
                    + bin
                    + " \\tikzexternalcheckshellescape -halt-on-error -interaction=batchmode"
                      " -output-directory=\""
                    + rel + "\" -jobname \"\\image\" \"\\texsource\"}}\\fi}";
            }
        }
        return preamble;
    }

    BuildResult buildWith(const BuildOptions& options, EngineKind engineKind,
        bool suppressResetNotice = false)
    {
        auto [engineArguments, flagJobName] = splitJobName(options.engineArguments);
        auto jobName = options.jobName ? options.jobName : flagJobName;
        auto engine = createEngine(engineKind, engineArguments);

        fs::path outDir = options.resolvedOutputDir();
        fs::create_directories(outDir);
        if (options.usesDefaultOutputDir())
            ignoreInVersionControl(outDir);
        mirrorSourceDirectories(options.texFile.parent_path(), outDir);
        fs::path dbPath = outDir / ".maketex.db";

        Blake3Hasher hasher;

        std::vector<std::unique_ptr<ITool>> tools;
        tools.reserve(options.tools.size() + options.customTools.size());
        for (ToolKind kind : options.tools)
            tools.push_back(createTool(kind));
        for (const auto& recipe : options.customTools)
            tools.push_back(std::make_unique<HashTriggeredTool>(recipe));

        EngineInvocation inv {
            .texFile = options.texFile,
            .outputDir = outDir,
            .jobName = jobName,
            .haltOnError = options.haltOnError,
            .shellEscape = options.shellEscape,
            .synctex = options.synctex,
            .preamble = buildPreambleHooks(options.texFile, outDir, engineKind,
                engineArguments.program, options.shellEscape),
            .extraEnv = resolvedEnv(options),
        };

        // Held until the build returns: a second maketex building this
        // document (watch plus a manual run) waits instead of interleaving
        // writes to the same .aux and PDF.
        BuildLock lock(buildLockPath(outDir, inv.resolvedJobName()));

        auto db = std::make_unique<SQLite::Database>(
            dbPath.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        StateDatabase stateDb(std::move(db),
            StateDbConfig {
                .projectRoot = options.texFile.parent_path(),
                .fingerprint = buildFingerprint(options, engine->name(), tools),
                .jobName = inv.resolvedJobName(),
            });

        std::vector<ITool*> toolPtrs;
        toolPtrs.reserve(tools.size());
        for (auto& t : tools)
            toolPtrs.push_back(t.get());

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
        };
        if (!options.contentFilters.empty()) {
            // Merge user filters with built-in defaults for the same extension
            // instead of overwriting them (e.g. keep \@writefile filtering on
            // .aux when the user adds another .aux ignore pattern).
            std::map<std::string, std::string> combined {
                { "eps", R"(^%%CreationDate: )" },
                { "aux", R"(^\\@writefile\{)" },
            };
            for (const auto& [ext, pattern] : options.contentFilters) {
                auto it = combined.find(ext);
                if (it == combined.end())
                    combined.emplace(ext, pattern);
                else
                    it->second = "(" + it->second + ")|(" + pattern + ")";
            }
            driverConfig.contentFilters.clear();
            for (const auto& [ext, pattern] : combined)
                driverConfig.contentFilters.emplace_back(ext, std::regex { pattern });
        }

        BuildDriver driver(*engine, stateDb, hasher, inv, std::move(driverConfig));
        BuildResult result;
        static_cast<DriverResult&>(result) = driver.run();
        result.engine = engineKind;

        if (stateDb.wasReset() && !suppressResetNotice)
            result.diagnostics.insert(result.diagnostics.begin(),
                Diagnostic {
                    .severity = Diagnostic::Severity::Info,
                    .message = "stored build state was incompatible "
                               "(different maketex version or build options) — "
                               "started from a full rebuild",
                });

        // Unconditional on success — also after an up-to-date skip, which
        // covers a published PDF the user deleted while the build state
        // stayed current.
        if (result.success && options.usesDefaultOutputDir())
            result.published = publishArtifacts(outDir, options.texFile.parent_path(),
                inv.resolvedJobName());
        else if (result.success)
            result.published.push_back(inv.jobFile(".pdf"));
        return result;
    }

} // namespace

std::string jobNameFor(const BuildOptions& options)
{
    if (options.jobName && !options.jobName->empty())
        return *options.jobName;
    return splitJobName(options.engineArguments).second.value_or(options.texFile.stem().string());
}

std::vector<fs::path> publishedArtifactPaths(const BuildOptions& options)
{
    if (!options.usesDefaultOutputDir())
        return { };
    auto texDir = options.texFile.parent_path();
    auto job = jobNameFor(options);
    return { texDir / (job + ".pdf"), texDir / (job + ".synctex.gz") };
}

fs::path buildLockPath(const fs::path& outDir, const std::string& jobName)
{
    return outDir / ("." + jobName + ".lock");
}

EngineChoice chooseEngine(const BuildOptions& options)
{
    if (options.engine)
        return { .kind = *options.engine, .reason = "" };
    return detectEngine(options.texFile);
}

BuildResult runBuild(const BuildOptions& options)
{
    auto choice = chooseEngine(options);
    // If no engine was configured or statically detected, reuse a Unicode
    // engine discovered by a previous retry so subsequent builds stay
    // incremental instead of failing pdflatex and resetting the database.
    if (!options.engine && choice.kind == EngineKind::PdfLaTeX && choice.reason.empty()) {
        if (auto remembered = loadDetectedEngine(options)) {
            choice.kind = *remembered;
            choice.reason = "remembered from previous build";
        }
    }

    auto result = buildWith(options, choice.kind);
    result.engineReason = choice.reason;

    // The preamble scan cannot see packages a custom class or style loads.
    // When the guess was plain pdflatex and the engine said why that was
    // wrong, retry once with the engine it asked for.
    bool guessed = !options.engine && choice.kind == EngineKind::PdfLaTeX;
    if (!result.success && guessed) {
        if (auto needed = unicodeEngineNeeded(result)) {
            // Drop any partial .aux left by the failed pdflatex pass so it
            // cannot confuse the Unicode engine.
            std::error_code ec;
            fs::remove(options.resolvedOutputDir() / (jobNameFor(options) + ".aux"), ec);

            auto retried = buildWith(options, *needed, /*suppressResetNotice=*/true);
            retried.engineReason = "pdflatex failed: the document needs a Unicode engine";
            retried.retriedEngine = true;
            if (retried.success)
                saveDetectedEngine(options, *needed);
            retried.diagnostics.insert(retried.diagnostics.begin(),
                Diagnostic {
                    .severity = Diagnostic::Severity::Info,
                    .message = std::format(
                        "the document needs {0} — rebuilt with it (set `engine = \"{0}\"` "
                        "in maketex.toml or add `% !TeX program = {0}` to skip the retry)",
                        engineKindName(*needed)),
                });
            return retried;
        }
    }
    return result;
}

namespace {

    bool sameContent(const fs::path& a, const fs::path& b)
    {
        std::error_code ec;
        auto size = fs::file_size(a, ec);
        if (ec || fs::file_size(b, ec) != size || ec)
            return false;
        std::ifstream fa(a, std::ios::binary);
        std::ifstream fb(b, std::ios::binary);
        std::array<char, 65536> ba;
        std::array<char, 65536> bb;
        while (fa && fb) {
            fa.read(ba.data(), ba.size());
            fb.read(bb.data(), bb.size());
            if (fa.gcount() != fb.gcount()
                || !std::equal(ba.begin(), ba.begin() + fa.gcount(), bb.begin()))
                return false;
        }
        return fa.eof() && fb.eof();
    }

} // namespace

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
        // Already published from this build: copying again would only bump
        // the mtime and make PDF viewers reload for nothing.
        if (sameContent(src, dest)) {
            published.push_back(std::move(dest));
            continue;
        }
        // Copy to a sibling temp file first: rename within destDir is atomic
        // on POSIX, so a viewer refreshing mid-publish sees old or new,
        // never a torn file. The temp name carries the pid so two processes
        // publishing at once never write the same temp file.
        fs::path tmp = dest;
        tmp += ".maketex-tmp-" + std::to_string(::getpid());
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
    auto flagJobName = splitJobName(options.engineArguments).second;
    EngineInvocation inv {
        .texFile = options.texFile,
        .outputDir = options.resolvedOutputDir(),
        .jobName = options.jobName ? options.jobName : flagJobName,
        .extraEnv = resolvedEnv(options),
    };
    fs::path flsPath = inv.jobFile(".fls");
    auto systemRoots = texSystemRoots();
    auto flsParser = projectFlsParser(options.texFile.parent_path(), systemRoots);

    auto record = flsParser.parse(flsPath);
    if (!record)
        return std::nullopt;

    std::unordered_set<std::string> outputPaths;
    for (const auto& p : record->outputs)
        outputPaths.insert(p.string());

    BuildFileSets sets;
    std::unordered_set<std::string> seenSources;
    for (const auto& p : record->inputs) {
        if (!outputPaths.contains(p.string()) && seenSources.insert(p.string()).second)
            sets.sources.push_back(p);
    }

    // Auxiliary tools read files the TeX engine never opens (.bib databases,
    // local .bst styles), so they are absent from the .fls. Include them as
    // sources so `maketex watch` rebuilds when a bibliography is edited.
    auto safeCanonical = [](const fs::path& p) {
        std::error_code ec;
        auto c = fs::weakly_canonical(p, ec);
        return ec ? p.lexically_normal() : c;
    };
    auto isUnder = [](const fs::path& p, const fs::path& root) {
        auto [a, b] = std::ranges::mismatch(root, p);
        return a == root.end();
    };
    const fs::path outDirCanonical = safeCanonical(inv.outputDir);
    std::vector<fs::path> systemRootsCanonical;
    systemRootsCanonical.reserve(systemRoots.size());
    for (const auto& r : systemRoots)
        systemRootsCanonical.push_back(safeCanonical(r));

    auto logFile = inv.jobFile(".log");
    auto auxFile = inv.jobFile(".aux");
    ToolContext ctx(
        inv,
        [logFile] { return LogParser { }.parse(logFile).value_or(LogRecord { }); },
        [auxFile] {
            std::error_code ec;
            if (!fs::exists(auxFile, ec))
                return AuxRecord { };
            return AuxParser { }.parse(auxFile).value_or(AuxRecord { });
        },
        [record] { return *record; });

    for (ToolKind kind : options.tools) {
        auto tool = createTool(kind);
        if (!tool->applies(ctx))
            continue;
        for (const auto& input : tool->inputs(ctx)) {
            if (input.content.has_value())
                continue;
            std::error_code ec;
            if (!fs::is_regular_file(input.path, ec))
                continue;
            auto canon = safeCanonical(input.path);
            if (outputPaths.contains(canon.string()) || isUnder(canon, outDirCanonical))
                continue;
            bool inSystem = std::ranges::any_of(systemRootsCanonical,
                [&](const fs::path& root) { return isUnder(canon, root); });
            if (inSystem)
                continue;
            if (seenSources.insert(canon.string()).second)
                sets.sources.push_back(std::move(canon));
        }
    }

    sets.generated = std::move(record->outputs);
    return sets;
}

} // namespace maketex
