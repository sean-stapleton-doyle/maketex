#include "maketex/BuildDriver.h"

#include "maketex/DiagnosticExtractor.h"
#include "maketex/FileStateTracker.h"
#include "maketex/IFileHasher.h"
#include "maketex/ITool.h"
#include "maketex/Launch.h"
#include "maketex/ToolContext.h"
#include "maketex/ToolInvocation.h"
#include "maketex/driver/FileDigests.h"
#include "maketex/driver/PassAnalysis.h"
#include "maketex/driver/ToolPass.h"
#include "maketex/graph/FileNode.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"
#include "maketex/tools/InputLocator.h"

#include <algorithm>
#include <ctime>
#include <iterator>
#include <unordered_set>
#include <utility>

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
    , m_processLimits(std::move(config.processLimits))
    , m_contentFilters(std::move(config.contentFilters))
    , m_diagnosticPolicy(std::move(config.diagnosticPolicy))
    , m_preambleProvider(std::move(config.preambleProvider))
    , m_draftFirstPass(config.draftFirstPass)
    , m_xelatexXdvSplit(config.xelatexXdvSplit)
    , m_processRunner(std::move(config.processRunner))
    , m_clock(std::move(config.clock))
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

std::int64_t BuildDriver::nowEpoch() const
{
    return m_clock ? m_clock() : static_cast<std::int64_t>(std::time(nullptr));
}

// ── helpers ───────────────────────────────────────────────────────────────────

namespace {

    std::optional<std::pair<fs::file_time_type, std::uintmax_t>> stampOf(const fs::path& path)
    {
        std::error_code ec;
        auto mtime = fs::last_write_time(path, ec);
        if (ec)
            return std::nullopt;
        auto size = fs::file_size(path, ec);
        if (ec)
            return std::nullopt;
        return std::pair { mtime, size };
    }

} // namespace

std::string BuildDriver::engineRuleName() const
{
    return std::string(m_engine.name()) + ":" + m_inv.resolvedJobName();
}

FlsParser BuildDriver::makeFlsParser() const
{
    return projectFlsParser(m_inv.texFile.parent_path(), m_ignorePrefixes);
}

FileStateTracker BuildDriver::makeTracker() const
{
    FileStateTracker tracker(m_db, m_hasher);
    for (const auto& [extension, pattern] : m_contentFilters)
        tracker.setIgnorePattern(extension, pattern);
    return tracker;
}

driver::ToolPass BuildDriver::makeToolPass(driver::FileDigests& digests) const
{
    return driver::ToolPass(
        driver::ToolPass::Deps {
            .db = m_db,
            .digests = digests,
            .runner = m_processRunner,
            .limits = m_processLimits,
            .observer = m_observer,
            .clock = m_clock,
        },
        m_inv);
}

ToolContext BuildDriver::makeToolContext() const
{
    auto logFile = m_inv.jobFile(".log");
    auto auxFile = m_inv.jobFile(".aux");
    auto flsFile = m_inv.jobFile(".fls");
    auto flsParser = makeFlsParser();
    return ToolContext(
        m_inv,
        [logFile] { return LogParser { }.parse(logFile).value_or(LogRecord { }); },
        [auxFile] {
            if (!fs::exists(auxFile))
                return AuxRecord { };
            return AuxParser { }.parse(auxFile).value_or(AuxRecord { });
        },
        [flsFile, flsParser] { return flsParser.parse(flsFile).value_or(FlsRecord { }); });
}

// ── BuildDriver::updateGraphFromFls ───────────────────────────────────────────

void BuildDriver::updateGraphFromFls(const fs::path& fls)
{
    auto parsed = makeFlsParser().parse(fls);
    if (!parsed.has_value())
        return; // .fls unreadable — nothing to wire up this pass
    const auto& rec = parsed.value();

    // Build a set of paths that appear as inputs so we can distinguish
    // output-only files (log, pdf) from read-write files (aux) that must
    // be snapshotted for convergence detection.
    std::unordered_set<std::string> inputPaths;
    for (const auto& p : rec.inputs)
        inputPaths.insert(driver::FileDigests::canonicalKey(p));

    // The .fls is the complete record of one pass: an \input the user
    // removed must stop being a dependency (and stop being persisted).
    auto& latexRule = m_graph.getOrAddRule(engineRuleName());
    latexRule.clearEdges();

    for (const auto& p : rec.inputs) {
        auto& node = m_graph.getOrAddFile(p, FileRole::Source);
        latexRule.addInput(&node);
    }
    for (const auto& p : rec.outputs) {
        if (inputPaths.contains(driver::FileDigests::canonicalKey(p))) {
            auto& node = m_graph.getOrAddFile(p, FileRole::Generated);
            node.setRole(FileRole::Generated);
            latexRule.addOutput(&node);
        } else {
            auto& node = m_graph.getOrAddFile(p, FileRole::Output);
            // A tool may have registered it first (as a tool input, e.g.
            // the .idx); the engine writing it settles its role.
            if (node.role() == FileRole::Source)
                node.setRole(FileRole::Output);
            latexRule.addOutput(&node);
        }
    }
}

// ── BuildDriver::engineUpToDate ──────────────────────────────────────────────

bool BuildDriver::engineUpToDate(FileStateTracker& tracker)
{
    auto fls = m_inv.jobFile(".fls");
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

    // Inputs are unchanged, but a deleted product (the PDF) or read-back
    // file (the .aux) still requires a rebuild — isStale only looks at
    // inputs. Scratch files a package writes and deletes in the same run
    // (minted's .pyg) are neither and must not block the skip.
    // A read-back file counts only if it existed when the build was
    // recorded: minted writes, reads, and deletes main.pyg in every pass.
    for (auto* node : latexRule->outputs()) {
        bool needed = driver::isFinalProduct(node->path())
            || (node->role() == FileRole::Generated
                && m_db.getRuleInputHash(engineRuleName(), node->path().string()));
        if (needed && !fs::exists(node->path()))
            return false;
    }
    // Under XeLaTeX -no-pdf split, the .fls records .xdv rather than .pdf:
    // verify the final PDF produced by xdvipdfmx still exists.
    if (m_xelatexXdvSplit && !fs::exists(m_inv.jobFile(".pdf")))
        return false;
    return true;
}

// ── convergence signals ───────────────────────────────────────────────────────

std::optional<std::string> BuildDriver::appearedInput(const LogRecord& log) const
{
    // Every cold run logs "No file <job>.aux.". What it writes there is
    // mostly checked already — LaTeX warns "Label(s) may have changed",
    // tools key on bibliography records — so a newly written .aux forces a
    // second pass only when it holds content nothing checks. That keeps a
    // simple document at one pass without trusting a package's custom
    // .aux data to a single run.
    auto ownAux = m_inv.resolvedJobName() + ".aux";
    std::error_code ec;
    for (const auto& name : log.missingFiles) {
        if (name == ownAux) {
            auto aux = m_inv.jobFile(".aux");
            if (fs::exists(aux, ec))
                if (auto rec = AuxParser { }.parse(aux); rec && rec->hasUncheckedContent)
                    return name + " holds package data LaTeX doesn't check";
            continue;
        }
        for (const auto& dir : { m_inv.outputDir, m_inv.texFile.parent_path() })
            if (fs::exists(dir / name, ec))
                return name + " now exists";
    }
    return std::nullopt;
}

BuildDriver::Stamps BuildDriver::stampKnownSources() const
{
    Stamps stamps;
    stamps[m_inv.texFile] = stampOf(m_inv.texFile);
    if (const auto* latexRule = m_graph.findRule(engineRuleName()))
        for (const auto* node : latexRule->inputs())
            if (node->role() == FileRole::Source)
                stamps[node->path()] = stampOf(node->path());
    return stamps;
}

std::optional<fs::path> BuildDriver::editedDuringPass(const Stamps& before)
{
    for (const auto& [path, stamp] : before)
        if (stampOf(path) != stamp)
            return path;
    return std::nullopt;
}

void BuildDriver::persistFileStates()
{
    std::vector<FileState> states;
    for (auto* f : m_graph.files()) {
        if (f->hasFreshState())
            states.push_back(f->state());
    }
    m_db.upsertFileStates(states);
}

// ── BuildDriver::run steps ────────────────────────────────────────────────────

void BuildDriver::recordToolFailure(DriverResult& result, driver::ToolPassOutcome& outcome)
{
    if (outcome.snapshotError) {
        result.lastError = "snapshot failed: " + outcome.snapshotError->message();
        return;
    }
    result.lastError = driver::failureMessage(outcome.failedToolName, *outcome.failure);
    result.diagnostics = diagnostics::extract(*outcome.failure);
}

std::optional<DriverResult> BuildDriver::finishWithoutEnginePass(driver::FileDigests& digests)
{
    // The engine's own inputs are unchanged, but a tool's may not be (.bib
    // files are read by bibtex, never by latex). Run just those tools; only if
    // one rewrites a file latex reads is a pass needed.
    auto ctx = makeToolContext();
    auto tools = makeToolPass(digests).run(m_tools, ctx);
    DriverResult result;
    if (tools.failure || tools.snapshotError) {
        recordToolFailure(result, tools);
        return result;
    }
    if (tools.outputsChanged)
        return std::nullopt;

    persistFileStates();
    result.diagnostics = std::move(tools.diagnostics);
    if (auto policyError = applyDiagnosticPolicy(result.diagnostics, m_diagnosticPolicy)) {
        result.lastError = std::move(*policyError);
        result.success = false;
        return result;
    }
    if (!tools.ranAny) {
        notify({ .kind = BuildEvent::Kind::SkippedUpToDate });
        result.upToDate = true;
    }
    result.success = true;
    return result;
}

void BuildDriver::ensurePreamble()
{
    if (m_inv.preamble.empty() && m_preambleProvider)
        m_inv.preamble = m_preambleProvider();
}

std::expected<void, std::string> BuildDriver::refreshEngineInputs(
    RuleNode& latexRule, FileStateTracker& tracker)
{
    if (auto fls = m_inv.jobFile(".fls"); fs::exists(fls))
        updateGraphFromFls(fls);
    for (auto* node : latexRule.inputs()) {
        if (auto r = node->refresh(tracker); !r)
            return std::unexpected("snapshot failed: " + r.error().message());
    }
    return { };
}

bool BuildDriver::finalizeXdv(RuleNode& latexRule,
    const std::string& command,
    ConvergenceState& state)
{
    auto xdv = m_inv.jobFile(".xdv");
    if (!fs::exists(xdv))
        return true;

    notify({ .kind = BuildEvent::Kind::ToolStarted, .toolName = "xdvipdfmx" });
    ToolInvocation xdvInv {
        .workingDir = m_inv.outputDir,
        .jobName = m_inv.resolvedJobName(),
        .outputDir = m_inv.outputDir,
        .sourceDir = m_inv.texFile.parent_path(),
    };
    std::vector<std::string> xdvArgs {
        "xdvipdfmx", "-o", m_inv.resolvedJobName() + ".pdf",
        m_inv.resolvedJobName() + ".xdv"
    };
    auto xdvEnv = toolSearchEnv(m_inv.texFile.parent_path(), m_inv.extraEnv);
    auto xdvRes = launchProcess(m_processRunner, xdvArgs, m_inv.outputDir,
        InvokerKind::Xdvipdfmx, std::move(xdvInv), xdvEnv, m_processLimits);
    notify({ .kind = BuildEvent::Kind::ToolFinished,
        .toolName = "xdvipdfmx",
        .success = xdvRes.success() });
    if (!xdvRes.success()) {
        latexRule.markRan(m_db, false, command);
        state.result.lastError = driver::failureMessage("xdvipdfmx", xdvRes);
        state.result.diagnostics = diagnostics::extract(xdvRes);
        return false;
    }
    std::ranges::move(diagnostics::extract(xdvRes), std::back_inserter(state.toolWarnings));
    return true;
}

void BuildDriver::finalizeConverged(const RunResult<EngineInvocation>& engineResult,
    RuleNode& latexRule,
    const std::string& command,
    ConvergenceState& state)
{
    if (m_xelatexXdvSplit && !finalizeXdv(latexRule, command, state))
        return;

    state.result.diagnostics = diagnostics::extract(engineResult);
    std::ranges::move(state.toolWarnings, std::back_inserter(state.result.diagnostics));
    if (auto policyError = applyDiagnosticPolicy(state.result.diagnostics, m_diagnosticPolicy)) {
        // Failed by diagnostic policy: don't record as succeeded so the next
        // invocation does not skip as "up to date".
        latexRule.markRan(m_db, false, command);
        persistFileStates();
        state.result.lastError = std::move(*policyError);
        state.result.success = false;
        return;
    }
    latexRule.markRan(m_db, true, command);
    persistFileStates();
    state.result.success = true;
}

BuildDriver::PassStep BuildDriver::runPass(int pass,
    FileStateTracker& tracker,
    driver::FileDigests& digests,
    RuleNode& latexRule,
    const std::string& command,
    ConvergenceState& state)
{
    auto before = digests.takeSnapshot();
    if (!before) {
        state.result.lastError = "snapshot failed: " + before.error().message();
        return PassStep::Done;
    }

    notify({ .kind = BuildEvent::Kind::PassStarted, .pass = pass + 1 });
    // Sources that exist before the pass; a file created during it
    // (epstopdf's figure-eps-converted-to.pdf) is generated, not edited.
    auto sourcesBefore = stampKnownSources();
    EngineInvocation passInv = m_inv;
    passInv.draftMode = (pass == 0 && m_draftFirstPass) || m_xelatexXdvSplit;
    auto engineResult = m_engine.run(passInv, m_processLimits);
    // Checked now, before tools rewrite their outputs (.bbl is a source the
    // engine reads): only the user's saves during the pass count.
    auto edited = editedDuringPass(sourcesBefore);
    ++state.result.passes;
    notify({ .kind = BuildEvent::Kind::PassFinished,
        .pass = pass + 1,
        .success = engineResult.success() });

    if (!engineResult.success()) {
        latexRule.markRan(m_db, false, command);
        state.result.lastError = driver::failureMessage("latex", engineResult);
        state.result.diagnostics = diagnostics::extract(engineResult);
        return PassStep::Done;
    }

    if (auto r = refreshEngineInputs(latexRule, tracker); !r) {
        state.result.lastError = std::move(r.error());
        return PassStep::Done;
    }
    auto after = digests.takeSnapshot();
    if (!after) {
        state.result.lastError = "snapshot failed: " + after.error().message();
        return PassStep::Done;
    }

    auto readBackChanged = driver::firstChangedReadBack(
        std::as_const(m_graph).files(), *before, *after);
    auto ctx = makeToolContext();
    auto tools = makeToolPass(digests).run(m_tools, ctx);
    if (tools.failure || tools.snapshotError) {
        latexRule.markRan(m_db, false, command);
        recordToolFailure(state.result, tools);
        return PassStep::Done;
    }
    std::ranges::move(tools.diagnostics, std::back_inserter(state.toolWarnings));

    auto reasons = driver::rerunReasons(driver::PassSignals {
        .pass = pass,
        .logAskedRerun = ctx.log().rerunNeeded,
        .readBackChanged = std::move(readBackChanged),
        .appearedInput = appearedInput(ctx.log()),
        .editedDuringPass = std::move(edited),
        .toolChanges = std::move(tools.changes),
        .draftFirstPass = m_draftFirstPass,
        .xdvSplit = m_xelatexXdvSplit,
    });
    if (!reasons.empty()) {
        state.result.rerunReasons.push_back(driver::formatRerunReason(pass + 2, reasons));
        notify({ .kind = BuildEvent::Kind::RerunScheduled,
            .pass = pass + 2,
            .detail = driver::joinRerunReasons(reasons) });
        return PassStep::Rerun;
    }

    finalizeConverged(engineResult, latexRule, command, state);
    return PassStep::Done;
}

DriverResult BuildDriver::run()
{
    if (m_maxPasses < 1)
        return DriverResult { .lastError = "max passes must be at least 1" };

    auto tracker = makeTracker();
    driver::FileDigests digests(m_graph, tracker, m_hasher);
    if (engineUpToDate(tracker)) {
        if (auto done = finishWithoutEnginePass(digests))
            return *std::move(done);
    }

    auto& latexRule = m_graph.getOrAddRule(engineRuleName());
    const auto command = m_inv.texFile.string();
    ensurePreamble();

    ConvergenceState state;
    for (int pass = 0; pass < m_maxPasses; ++pass) {
        if (runPass(pass, tracker, digests, latexRule, command, state) == PassStep::Done)
            return std::move(state.result);
    }

    // A non-converged PDF must not count as current: the next invocation has
    // to rebuild rather than report "up to date".
    latexRule.markRan(m_db, false, command);
    state.result.lastError = "did not converge after " + std::to_string(m_maxPasses) + " passes";
    return std::move(state.result);
}

} // namespace maketex
