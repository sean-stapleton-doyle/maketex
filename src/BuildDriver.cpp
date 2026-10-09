#include "maketex/BuildDriver.h"

#include "maketex/DiagnosticExtractor.h"
#include "maketex/FileStateTracker.h"
#include "maketex/IFileHasher.h"
#include "maketex/ITool.h"
#include "maketex/Launch.h"
#include "maketex/RunDecision.h"
#include "maketex/ToolContext.h"
#include "maketex/ToolInvocation.h"
#include "maketex/graph/FileNode.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>

namespace maketex {

namespace fs = std::filesystem;

BuildDriver::BuildDriver(IEngine& engine,
    StateDatabase& db,
    IFileHasher& hasher,
    EngineInvocation inv,
    Config config)
    : m_engine(engine)
    , m_db(db)
    , m_hasher(hasher)
    , m_inv(std::move(inv))
    , m_maxPasses(config.maxPasses)
    , m_tools(std::move(config.tools))
    , m_ignorePrefixes(std::move(config.ignorePrefixes))
    , m_observer(std::move(config.observer))
{
}

BuildDriver::BuildDriver(IEngine& engine,
    StateDatabase& db,
    IFileHasher& hasher,
    EngineInvocation inv)
    : BuildDriver(engine, db, hasher, std::move(inv), Config { })
{
}

void BuildDriver::notify(const BuildEvent& event) const
{
    if (m_observer)
        m_observer(event);
}

// ── helpers ───────────────────────────────────────────────────────────────────

static std::filesystem::path flsPath(const EngineInvocation& inv)
{
    return inv.outputDir / (inv.resolvedJobName() + ".fls");
}

static std::filesystem::path logPath(const EngineInvocation& inv)
{
    return inv.outputDir / (inv.resolvedJobName() + ".log");
}

static std::filesystem::path auxPath(const EngineInvocation& inv)
{
    return inv.outputDir / (inv.resolvedJobName() + ".aux");
}

static ToolInvocation makeToolInv(const EngineInvocation& inv)
{
    return ToolInvocation {
        .workingDir = inv.outputDir,
        .jobName = inv.resolvedJobName(),
        .outputDir = inv.outputDir,
    };
}

std::string BuildDriver::engineRuleName() const
{
    return std::string(m_engine.name()) + ":" + m_inv.resolvedJobName();
}

std::string BuildDriver::toolRuleName(const ITool& tool) const
{
    return "tool:" + std::string(tool.name()) + ":" + m_inv.resolvedJobName();
}

ToolContext BuildDriver::makeToolContext() const
{
    auto logFile = logPath(m_inv);
    auto auxFile = auxPath(m_inv);
    return ToolContext(m_inv, [logFile]() { return LogParser { }.parse(logFile).value_or(LogRecord { }); }, [auxFile]() {
            if (!fs::exists(auxFile))
                return AuxRecord {};
            return AuxParser {}.parse(auxFile).value_or(AuxRecord {}); });
}

// ── BuildDriver::updateGraphFromFls ───────────────────────────────────────────

void BuildDriver::updateGraphFromFls(const std::filesystem::path& fls)
{
    FlsParser parser;
    // The .fls may predate a directory move (a restored CI cache): re-root
    // its recorded paths onto where the document lives now.
    parser.overridePwd(m_inv.texFile.parent_path());
    for (const auto& prefix : m_ignorePrefixes)
        parser.addIgnorePrefix(prefix);
    auto parsed = parser.parse(fls);
    if (!parsed.has_value())
        return; // .fls unreadable — nothing to wire up this pass
    const auto& rec = parsed.value();

    // Build a set of paths that appear as inputs so we can distinguish
    // output-only files (log, pdf) from read-write files (aux) that must
    // be snapshotted for convergence detection.
    std::unordered_set<std::string> inputPaths;
    for (const auto& p : rec.inputs)
        inputPaths.insert(fs::weakly_canonical(p).string());

    auto& latexRule = m_graph.getOrAddRule(engineRuleName());

    for (const auto& p : rec.inputs) {
        auto& node = m_graph.getOrAddFile(p, FileRole::Source);
        latexRule.addInput(&node);
    }
    for (const auto& p : rec.outputs) {
        auto key = fs::weakly_canonical(p).string();
        if (inputPaths.count(key)) {
            auto& node = m_graph.getOrAddFile(p, FileRole::Generated);
            node.setRole(FileRole::Generated);
            latexRule.addOutput(&node);
        } else {
            auto& node = m_graph.getOrAddFile(p, FileRole::Output);
            latexRule.addOutput(&node);
        }
    }
}

// ── BuildDriver::takeSnapshot ─────────────────────────────────────────────────

std::expected<std::unordered_map<std::string, Hash>, std::error_code>
BuildDriver::takeSnapshot(FileStateTracker& tracker)
{
    std::unordered_map<std::string, Hash> snap;
    for (auto* f : m_graph.files()) {
        if (f->role() != FileRole::Generated)
            continue;
        auto state = f->refresh(tracker);
        if (!state)
            return std::unexpected(state.error());
        if (state->exists)
            snap[f->path().string()] = state->hash;
    }
    return snap;
}

// ── BuildDriver::tryIncrementalSkip ──────────────────────────────────────────

bool BuildDriver::tryIncrementalSkip(FileStateTracker& tracker)
{
    auto fls = flsPath(m_inv);
    if (!fs::exists(fls))
        return false;

    updateGraphFromFls(fls);

    auto* latexRule = m_graph.findRule(engineRuleName());
    if (!latexRule || latexRule->inputs().empty())
        return false;

    for (auto* node : latexRule->inputs())
        if (auto r = node->refresh(tracker); !r)
            return false; // snapshot error — fall through to a real build pass
    if (latexRule->isStale(m_db))
        return false;

    // Inputs are unchanged, but a deleted output (the PDF, the .aux) still
    // requires a rebuild — isStale only looks at inputs.
    for (auto* node : latexRule->outputs()) {
        if (!fs::exists(node->path()))
            return false;
    }

    // The engine's inputs don't cover auxiliary-tool dependencies: .bib files
    // are read by bibtex, not latex, and never appear in the .fls. Each tool
    // that would fire now must prove its recorded inputs are unchanged since
    // the last successful build.
    if (m_tools.empty())
        return true;

    auto ctx = makeToolContext();
    for (auto* tool : m_tools) {
        if (tool->decide(ctx) != RunDecision::Run)
            continue;

        auto ruleName = toolRuleName(*tool);
        auto& toolRule = m_graph.getOrAddRule(ruleName);
        for (const auto& p : tool->trackedInputs(ctx))
            toolRule.addInput(&m_graph.getOrAddFile(p, FileRole::Source));
        for (auto* node : toolRule.inputs())
            if (auto r = node->refresh(tracker); !r)
                return false; // snapshot error — fall through to a real build
        if (toolRule.isStale(m_db))
            return false;

        // isStale walks the rule's *current* inputs; a recorded input that no
        // longer exists (e.g. a deleted .bib) is invisible to it.
        for (const auto& [path, hash] : m_db.getRuleInputs(ruleName)) {
            if (!fs::exists(path))
                return false;
        }
    }
    return true;
}

// ── BuildDriver::persistEnginePass ───────────────────────────────────────────

std::expected<void, std::error_code> BuildDriver::persistEnginePass(
    FileStateTracker& tracker, RuleNode& latexRule)
{
    // markRan happens after updateGraphFromFls so the rule's input list
    // reflects what the engine actually read this pass. Inputs must be
    // refresh()ed so their on-disk hashes are persisted for the next
    // invocation's isStale check.
    auto fls = flsPath(m_inv);
    if (fs::exists(fls))
        updateGraphFromFls(fls);
    for (auto* node : latexRule.inputs())
        if (auto r = node->refresh(tracker); !r)
            return std::unexpected(r.error());
    latexRule.markRan(m_db, true, m_inv.texFile.string());
    return { };
}

// ── BuildDriver::persistFileStates ───────────────────────────────────────────

void BuildDriver::persistFileStates()
{
    std::vector<FileState> states;
    for (auto* f : m_graph.files()) {
        if (f->hasFreshState())
            states.push_back(f->state());
    }
    m_db.upsertFileStates(states);
}

// ── BuildDriver::runAuxiliaryTools ───────────────────────────────────────────

BuildDriver::ToolPassOutcome BuildDriver::runAuxiliaryTools(ToolContext& ctx)
{
    // Tools run from the output directory (their .aux/.idx inputs live
    // there), which hides source-adjacent files — .bib, .bst, .ist — once
    // the output directory is not the tex directory. These kpathsea search
    // paths re-expose it; the trailing ':' appends the system defaults
    // (the same mechanism latexmk uses with -outdir).
    const std::string texDir = m_inv.texFile.parent_path().string() + ":";
    const std::vector<std::pair<std::string, std::string>> toolEnv = {
        { "BIBINPUTS", texDir },
        { "BSTINPUTS", texDir },
        { "TEXINPUTS", texDir },
        { "INDEXSTYLE", texDir },
    };

    ToolPassOutcome outcome;
    for (auto* tool : m_tools) {
        if (tool->decide(ctx) != RunDecision::Run)
            continue;
        notify({ .kind = BuildEvent::Kind::ToolStarted,
            .toolName = std::string(tool->name()) });
        auto toolInv = makeToolInv(m_inv);
        // Evaluated before the launchProcess call: its by-value invocation
        // parameter may be move-constructed before sibling arguments read
        // toolInv (argument evaluation is indeterminately sequenced).
        auto args = tool->buildArgs(toolInv);
        auto workingDir = toolInv.workingDir;
        auto toolResult = launchProcess(args, workingDir, tool->invokerKind(),
            std::move(toolInv), toolEnv);
        notify({ .kind = BuildEvent::Kind::ToolFinished,
            .toolName = std::string(tool->name()),
            .success = toolResult.success() });
        if (!toolResult.success()) {
            outcome.failedToolName = std::string(tool->name());
            outcome.failure = std::move(toolResult);
            return outcome;
        }
        tool->onSuccess(ctx);
        outcome.filesChanged = true;
        outcome.ranTools.push_back(tool);
    }
    return outcome;
}

// ── BuildDriver::persistToolRules ─────────────────────────────────────────────

std::expected<void, std::error_code> BuildDriver::persistToolRules(
    FileStateTracker& tracker,
    const ToolContext& ctx,
    const std::vector<ITool*>& ranTools)
{
    for (auto* tool : ranTools) {
        auto& rule = m_graph.getOrAddRule(toolRuleName(*tool));
        for (const auto& p : tool->trackedInputs(ctx))
            rule.addInput(&m_graph.getOrAddFile(p, FileRole::Source));
        for (auto* node : rule.inputs())
            if (auto r = node->refresh(tracker); !r)
                return std::unexpected(r.error());
        rule.markRan(m_db, true, std::string(tool->name()));
    }
    return { };
}

// ── BuildDriver::run ─────────────────────────────────────────────────────────

DriverResult BuildDriver::run()
{
    FileStateTracker tracker(m_db, m_hasher);
    // EPS files regenerated by graphics pipelines differ only in their
    // %%CreationDate stamp; hashing it would defeat convergence detection.
    tracker.setIgnorePattern("eps", std::regex { R"(^%%CreationDate: )" });

    DriverResult result;

    if (tryIncrementalSkip(tracker)) {
        // Persist any freshly computed states so the next invocation's
        // (mtime, size) fast path can skip rehashing.
        persistFileStates();
        notify({ .kind = BuildEvent::Kind::SkippedUpToDate });
        result.success = true;
        return result;
    }

    std::vector<ITool*> ranTools;

    for (int pass = 0; pass < m_maxPasses; ++pass) {
        auto beforeSnapshot = takeSnapshot(tracker);
        if (!beforeSnapshot) {
            result.lastError = "snapshot failed: " + beforeSnapshot.error().message();
            return result;
        }

        notify({ .kind = BuildEvent::Kind::PassStarted, .pass = pass + 1 });
        auto engineResult = m_engine.run(m_inv);
        ++result.passes;
        notify({ .kind = BuildEvent::Kind::PassFinished,
            .pass = pass + 1,
            .success = engineResult.success() });

        auto& latexRule = m_graph.getOrAddRule(engineRuleName());

        if (!engineResult.success()) {
            latexRule.markRan(m_db, false, m_inv.texFile.string());
            result.lastError = "latex exited with code "
                + std::to_string(engineResult.exitCode);
            result.diagnostics = diagnostics::extract(engineResult);
            return result;
        }

        if (auto r = persistEnginePass(tracker, latexRule); !r) {
            result.lastError = "snapshot failed: " + r.error().message();
            return result;
        }

        auto afterSnapshot = takeSnapshot(tracker);
        if (!afterSnapshot) {
            result.lastError = "snapshot failed: " + afterSnapshot.error().message();
            return result;
        }
        bool filesChanged = (*afterSnapshot != *beforeSnapshot);

        auto ctx = makeToolContext();

        auto toolOutcome = runAuxiliaryTools(ctx);
        if (toolOutcome.failure) {
            result.lastError = toolOutcome.failedToolName + " failed with exit code "
                + std::to_string(toolOutcome.failure->exitCode);
            result.diagnostics = diagnostics::extract(*toolOutcome.failure);
            return result;
        }
        for (auto* tool : toolOutcome.ranTools) {
            if (std::find(ranTools.begin(), ranTools.end(), tool) == ranTools.end())
                ranTools.push_back(tool);
        }
        filesChanged = filesChanged || toolOutcome.filesChanged;

        if (!ctx.log().rerunNeeded && !filesChanged) {
            // Tool input hashes are recorded only now, post-convergence, so
            // they reflect the final on-disk contents (latex rewrites .bcf
            // and .aux on every pass).
            if (auto r = persistToolRules(tracker, ctx, ranTools); !r) {
                result.lastError = "snapshot failed: " + r.error().message();
                return result;
            }
            persistFileStates();
            result.success = true;
            result.diagnostics = diagnostics::extract(engineResult);
            return result;
        }
    }

    result.lastError = "did not converge after "
        + std::to_string(m_maxPasses) + " passes";
    return result;
}

} // namespace maketex
