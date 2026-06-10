#include "BuildDriver.h"

#include "Blake3Hasher.h"
#include "DiagnosticExtractor.h"
#include "FileStateTracker.h"
#include "ITool.h"
#include "RunDecision.h"
#include "ToolContext.h"
#include "ToolInvocation.h"
#include "ToolRunner.h"
#include "graph/FileNode.h"
#include "parsers/AuxParser.h"
#include "parsers/FlsParser.h"
#include "parsers/LogParser.h"

#include <cassert>
#include <unordered_set>

namespace maketex {

namespace fs = std::filesystem;

BuildDriver::BuildDriver(IEngine& engine,
    StateDatabase& db,
    Blake3Hasher& hasher,
    EngineInvocation inv,
    int maxPasses,
    std::vector<ITool*> tools,
    ToolRunner* toolRunner)
    : m_engine(engine)
    , m_db(db)
    , m_hasher(hasher)
    , m_inv(std::move(inv))
    , m_maxPasses(maxPasses)
    , m_tools(std::move(tools))
    , m_toolRunner(toolRunner)
{
    assert((m_tools.empty() || m_toolRunner != nullptr)
        && "BuildDriver: tools require a non-null ToolRunner");
}

// ── helpers ───────────────────────────────────────────────────────────────────

static std::string jobName(const EngineInvocation& inv)
{
    return inv.jobName.value_or(inv.texFile.stem().string());
}

static std::filesystem::path flsPath(const EngineInvocation& inv)
{
    return inv.outputDir / (jobName(inv) + ".fls");
}

static std::filesystem::path logPath(const EngineInvocation& inv)
{
    return inv.outputDir / (jobName(inv) + ".log");
}

static std::filesystem::path auxPath(const EngineInvocation& inv)
{
    return inv.outputDir / (jobName(inv) + ".aux");
}

static ToolInvocation makeToolInv(const EngineInvocation& inv)
{
    return ToolInvocation {
        .workingDir = inv.outputDir,
        .jobName = jobName(inv),
        .outputDir = inv.outputDir,
    };
}

// ── BuildDriver::updateGraphFromFls ───────────────────────────────────────────

void BuildDriver::updateGraphFromFls(const std::filesystem::path& fls,
    const std::filesystem::path& /*pwd*/)
{
    FlsParser parser;
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

    auto& latexRule = m_graph.getOrAddRule(std::string(m_engine.name()));

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

std::unordered_map<std::string, Hash> BuildDriver::takeSnapshot(FileStateTracker& tracker)
{
    std::unordered_map<std::string, Hash> snap;
    for (auto* f : m_graph.files()) {
        if (f->role() == FileRole::Generated) {
            f->refresh(tracker);
            if (f->state().exists)
                snap[f->path().string()] = f->state().hash;
        }
    }
    return snap;
}

// ── BuildDriver::tryIncrementalSkip ──────────────────────────────────────────

bool BuildDriver::tryIncrementalSkip(FileStateTracker& tracker)
{
    auto fls = flsPath(m_inv);
    if (!fs::exists(fls))
        return false;

    updateGraphFromFls(fls, m_inv.outputDir);

    auto* latexRule = m_graph.findRule(std::string(m_engine.name()));
    if (!latexRule || latexRule->inputs().empty())
        return false;

    for (auto* node : latexRule->inputs())
        node->refresh(tracker);
    return !latexRule->isStale(m_db);
}

// ── BuildDriver::persistEnginePass ───────────────────────────────────────────

void BuildDriver::persistEnginePass(FileStateTracker& tracker, RuleNode& latexRule)
{
    // markRan happens after updateGraphFromFls so the rule's input list
    // reflects what the engine actually read this pass. Inputs must be
    // refresh()ed so their on-disk hashes are persisted for the next
    // invocation's isStale check.
    auto fls = flsPath(m_inv);
    if (fs::exists(fls))
        updateGraphFromFls(fls, m_inv.outputDir);
    for (auto* node : latexRule.inputs())
        node->refresh(tracker);
    latexRule.markRan(m_db, true, m_inv.texFile.string());
}

// ── BuildDriver::runAuxiliaryTools ───────────────────────────────────────────

BuildDriver::ToolPassOutcome BuildDriver::runAuxiliaryTools(ToolContext& ctx)
{
    ToolPassOutcome outcome;
    for (auto* tool : m_tools) {
        if (tool->decide(ctx) != RunDecision::Run)
            continue;
        auto toolResult = m_toolRunner->run(*tool, makeToolInv(m_inv)).get();
        if (!toolResult.success()) {
            outcome.failedToolName = std::string(tool->name());
            outcome.failure = std::move(toolResult);
            return outcome;
        }
        tool->onSuccess(ctx);
        outcome.filesChanged = true;
    }
    return outcome;
}

// ── BuildDriver::run ─────────────────────────────────────────────────────────

DriverResult BuildDriver::run()
{
    FileStateTracker tracker(m_db, m_hasher);
    DriverResult result;

    if (tryIncrementalSkip(tracker)) {
        result.success = true;
        return result;
    }

    for (int pass = 0; pass < m_maxPasses; ++pass) {
        auto beforeSnapshot = takeSnapshot(tracker);

        auto engineResult = m_engine.run(m_inv).get();
        ++result.passes;

        auto& latexRule = m_graph.getOrAddRule(std::string(m_engine.name()));

        if (!engineResult.success()) {
            latexRule.markRan(m_db, false, m_inv.texFile.string());
            result.lastError = "latex exited with code "
                + std::to_string(engineResult.exitCode);
            result.diagnostics = diagnostics::extract(engineResult);
            return result;
        }

        persistEnginePass(tracker, latexRule);

        bool filesChanged = (takeSnapshot(tracker) != beforeSnapshot);

        auto logFile = logPath(m_inv);
        auto auxFile = auxPath(m_inv);
        ToolContext ctx(m_inv, m_hasher,
            [logFile]() {
                return LogParser {}.parse(logFile).value_or(LogRecord {});
            },
            [auxFile]() {
                if (!fs::exists(auxFile))
                    return AuxRecord {};
                return AuxParser {}.parse(auxFile).value_or(AuxRecord {});
            });

        auto toolOutcome = runAuxiliaryTools(ctx);
        if (toolOutcome.failure) {
            result.lastError = toolOutcome.failedToolName + " failed with exit code "
                + std::to_string(toolOutcome.failure->exitCode);
            result.diagnostics = diagnostics::extract(*toolOutcome.failure);
            return result;
        }
        filesChanged = filesChanged || toolOutcome.filesChanged;

        if (!ctx.log().rerunNeeded && !filesChanged) {
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
