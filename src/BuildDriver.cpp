#include "maketex/BuildDriver.h"

#include "maketex/DiagnosticExtractor.h"
#include "maketex/FileStateTracker.h"
#include "maketex/IFileHasher.h"
#include "maketex/ITool.h"
#include "maketex/Launch.h"
#include "maketex/ToolContext.h"
#include "maketex/ToolInvocation.h"
#include "maketex/graph/FileNode.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"
#include "maketex/tools/InputLocator.h"

#include <algorithm>
#include <ctime>
#include <iterator>
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
    , m_processLimits(std::move(config.processLimits))
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

namespace {

    ToolInvocation makeToolInv(const EngineInvocation& inv)
    {
        return ToolInvocation {
            .workingDir = inv.outputDir,
            .jobName = inv.resolvedJobName(),
            .outputDir = inv.outputDir,
        };
    }

    // Files the engine writes for the user or for maketex, never to read
    // back: excluded from convergence snapshots (hashing a large PDF every
    // pass would be pure cost).
    bool isFinalProduct(const fs::path& p)
    {
        auto name = p.filename().string();
        for (std::string_view ext : { ".pdf", ".dvi", ".xdv", ".log", ".fls",
                 ".synctex.gz", ".synctex" })
            if (name.ends_with(ext))
                return true;
        return false;
    }

    // "biber is not installed", "latex exited with code 1", …
    template <typename Inv>
    std::string failureMessage(const std::string& what, const RunResult<Inv>& r)
    {
        if (r.notFound)
            return r.program + " is not installed (not found on PATH)";
        if (r.cancelled)
            return "cancelled";
        if (r.timedOut)
            return what + " timed out";
        return what + " exited with code " + std::to_string(r.exitCode);
    }

    std::string key(const fs::path& p)
    {
        return fs::weakly_canonical(p).string();
    }

    void recordRule(StateDatabase& db, const std::string& name, bool succeeded,
        const std::string& command)
    {
        db.upsertRule(RuleRecord {
            .name = name,
            .command = command,
            .ran_at = static_cast<int64_t>(std::time(nullptr)),
            .succeeded = succeeded,
        });
    }

} // namespace

std::string BuildDriver::engineRuleName() const
{
    return std::string(m_engine.name()) + ":" + m_inv.resolvedJobName();
}

std::string BuildDriver::toolRuleName(const ITool& tool) const
{
    return "tool:" + std::string(tool.name()) + ":" + m_inv.resolvedJobName();
}

FlsParser BuildDriver::makeFlsParser() const
{
    FlsParser parser;
    // The .fls may predate a directory move (a restored CI cache): re-root
    // its recorded paths onto where the document lives now.
    parser.overridePwd(m_inv.texFile.parent_path());
    for (const auto& prefix : m_ignorePrefixes)
        parser.addIgnorePrefix(prefix);
    return parser;
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
        inputPaths.insert(key(p));

    auto& latexRule = m_graph.getOrAddRule(engineRuleName());

    for (const auto& p : rec.inputs) {
        auto& node = m_graph.getOrAddFile(p, FileRole::Source);
        latexRule.addInput(&node);
    }
    for (const auto& p : rec.outputs) {
        if (inputPaths.contains(key(p))) {
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

// ── BuildDriver::takeSnapshot ─────────────────────────────────────────────────

std::expected<BuildDriver::Digests, std::error_code>
BuildDriver::takeSnapshot(FileStateTracker& tracker)
{
    Digests snap;
    for (auto* f : m_graph.files()) {
        if (f->role() == FileRole::Source || isFinalProduct(f->path()))
            continue;
        auto state = f->refresh(tracker);
        if (!state)
            return std::unexpected(state.error());
        if (state->exists)
            snap[f->path().string()] = state->hash;
    }
    return snap;
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
    for (auto* node : latexRule->outputs()) {
        bool needed = node->role() == FileRole::Generated || isFinalProduct(node->path());
        if (needed && !fs::exists(node->path()))
            return false;
    }
    return true;
}

// ── tool scheduling ───────────────────────────────────────────────────────────

std::expected<std::optional<Hash>, std::error_code> BuildDriver::currentHash(
    const fs::path& path, FileStateTracker& tracker)
{
    auto& node = m_graph.getOrAddFile(path, FileRole::Source);
    auto state = node.refresh(tracker);
    if (!state)
        return std::unexpected(state.error());
    if (!state->exists)
        return std::optional<Hash> { };
    return std::optional<Hash> { state->hash };
}

std::expected<BuildDriver::Digests, std::error_code> BuildDriver::digestInputs(
    const std::vector<ToolInput>& inputs, FileStateTracker& tracker)
{
    Digests digests;
    for (const auto& input : inputs) {
        if (input.content) {
            digests[key(input.path)] = m_hasher.hashBytes(*input.content);
            continue;
        }
        auto hash = currentHash(input.path, tracker);
        if (!hash)
            return std::unexpected(hash.error());
        if (*hash)
            digests[key(input.path)] = **hash;
    }
    return digests;
}

std::expected<bool, std::error_code> BuildDriver::toolIsStale(const ITool& tool,
    const Digests& inputs, FileStateTracker& tracker)
{
    auto ruleName = toolRuleName(tool);
    auto record = m_db.getRule(ruleName);
    if (!record || !record->succeeded)
        return true;

    Digests recorded;
    for (auto& [path, hash] : m_db.getRuleInputs(ruleName))
        recorded.emplace(std::move(path), hash);
    if (recorded != inputs)
        return true;

    // The products of that run must still be what it wrote: a deleted or
    // hand-edited .bbl is regenerated.
    for (const auto& [path, hash] : m_db.getRuleInputs(ruleName + "#outputs")) {
        auto current = currentHash(path, tracker);
        if (!current)
            return std::unexpected(current.error());
        if (!*current || **current != hash)
            return true;
    }
    return false;
}

BuildDriver::ToolPassOutcome BuildDriver::runStaleTools(
    const ToolContext& ctx, FileStateTracker& tracker)
{
    const auto toolEnv = toolSearchEnv(m_inv.texFile.parent_path());

    ToolPassOutcome outcome;
    for (auto* tool : m_tools) {
        if (!tool->applies(ctx))
            continue;
        auto inputs = digestInputs(tool->inputs(ctx), tracker);
        if (!inputs) {
            outcome.snapshotError = inputs.error();
            return outcome;
        }
        auto stale = toolIsStale(*tool, *inputs, tracker);
        if (!stale) {
            outcome.snapshotError = stale.error();
            return outcome;
        }
        if (!*stale)
            continue;

        auto outputs = tool->outputs(ctx);
        std::vector<std::optional<Hash>> before;
        for (const auto& out : outputs) {
            auto hash = currentHash(out, tracker);
            if (!hash) {
                outcome.snapshotError = hash.error();
                return outcome;
            }
            before.push_back(*hash);
        }

        notify({ .kind = BuildEvent::Kind::ToolStarted,
            .toolName = std::string(tool->name()) });
        auto toolInv = makeToolInv(m_inv);
        // Evaluated before the launchProcess call: its by-value invocation
        // parameter may be move-constructed before sibling arguments read
        // toolInv (argument evaluation is indeterminately sequenced).
        auto args = tool->buildArgs(toolInv);
        auto workingDir = toolInv.workingDir;
        auto toolResult = launchProcess(args, workingDir, tool->invokerKind(),
            std::move(toolInv), toolEnv, m_processLimits);
        notify({ .kind = BuildEvent::Kind::ToolFinished,
            .toolName = std::string(tool->name()),
            .success = toolResult.success() });

        auto ruleName = toolRuleName(*tool);
        if (!toolResult.success()) {
            recordRule(m_db, ruleName, false, std::string(tool->name()));
            outcome.failedToolName = std::string(tool->name());
            outcome.failure = std::move(toolResult);
            return outcome;
        }

        // Record the inputs this run consumed — hashed before it started —
        // and the outputs it produced, so neither a later pass nor the next
        // invocation reruns it unless one of them changes.
        std::vector<std::pair<std::string, Hash>> producedOutputs;
        bool changed = outputs.empty();
        for (size_t i = 0; i < outputs.size(); ++i) {
            auto after = currentHash(outputs[i], tracker);
            if (!after) {
                outcome.snapshotError = after.error();
                return outcome;
            }
            if (*after != before[i])
                changed = true;
            if (*after)
                producedOutputs.emplace_back(key(outputs[i]), **after);
        }
        recordRule(m_db, ruleName, true, std::string(tool->name()));
        m_db.setRuleInputs(ruleName, { inputs->begin(), inputs->end() });
        recordRule(m_db, ruleName + "#outputs", true, std::string(tool->name()));
        m_db.setRuleInputs(ruleName + "#outputs", producedOutputs);

        outcome.ranAny = true;
        outcome.outputsChanged = outcome.outputsChanged || changed;
        std::ranges::move(diagnostics::extract(toolResult), std::back_inserter(outcome.diagnostics));
    }
    return outcome;
}

// ── convergence signals ───────────────────────────────────────────────────────

bool BuildDriver::missingInputAppeared(const LogRecord& log) const
{
    // Every cold run logs "No file <job>.aux.". The .aux's effect on the
    // next pass is checked by LaTeX itself (it warns "Label(s) may have
    // changed" when labels moved), so a newly written .aux is not by itself
    // a reason to rerun — that is what makes a simple document one pass.
    auto ownAux = m_inv.resolvedJobName() + ".aux";
    std::error_code ec;
    for (const auto& name : log.missingFiles) {
        if (name == ownAux)
            continue;
        for (const auto& dir : { m_inv.outputDir, m_inv.texFile.parent_path() })
            if (fs::exists(dir / name, ec))
                return true;
    }
    return false;
}

std::vector<fs::path> BuildDriver::knownSources() const
{
    std::vector<fs::path> sources { m_inv.texFile };
    if (const auto* latexRule = m_graph.findRule(engineRuleName()))
        for (const auto* node : latexRule->inputs())
            if (node->role() == FileRole::Source)
                sources.push_back(node->path());
    return sources;
}

bool BuildDriver::sourceEditedDuringPass(const std::vector<fs::path>& sources,
    fs::file_time_type passStart) const
{
    std::error_code ec;
    for (const auto& path : sources) {
        // Strictly later: on a filesystem with coarse timestamps an edit in
        // the same tick is missed rather than looping forever.
        auto mtime = fs::last_write_time(path, ec);
        if (!ec && mtime > passStart)
            return true;
    }
    return false;
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

// ── BuildDriver::run ─────────────────────────────────────────────────────────

DriverResult BuildDriver::run()
{
    FileStateTracker tracker(m_db, m_hasher);
    // EPS files regenerated by graphics pipelines differ only in their
    // %%CreationDate stamp; hashing it would defeat convergence detection.
    tracker.setIgnorePattern("eps", std::regex { R"(^%%CreationDate: )" });

    DriverResult result;
    if (m_maxPasses < 1) {
        result.lastError = "max passes must be at least 1";
        return result;
    }

    auto toolFailure = [&](ToolPassOutcome& outcome) {
        if (outcome.snapshotError) {
            result.lastError = "snapshot failed: " + outcome.snapshotError->message();
            return;
        }
        result.lastError = failureMessage(outcome.failedToolName, *outcome.failure);
        result.diagnostics = diagnostics::extract(*outcome.failure);
    };

    if (engineUpToDate(tracker)) {
        // The engine's own inputs are unchanged, but a tool's may not be
        // (.bib files are read by bibtex, never by latex). Run just those
        // tools; only if one rewrites a file latex reads is a pass needed.
        auto ctx = makeToolContext();
        auto tools = runStaleTools(ctx, tracker);
        if (tools.failure || tools.snapshotError) {
            toolFailure(tools);
            return result;
        }
        if (!tools.outputsChanged) {
            // Persist any freshly computed states so the next invocation's
            // (mtime, size) fast path can skip rehashing.
            persistFileStates();
            result.diagnostics = std::move(tools.diagnostics);
            if (!tools.ranAny) {
                notify({ .kind = BuildEvent::Kind::SkippedUpToDate });
                result.upToDate = true;
            }
            result.success = true;
            return result;
        }
    }

    auto& latexRule = m_graph.getOrAddRule(engineRuleName());
    const auto command = m_inv.texFile.string();
    // Tool warnings collected across passes; reported with the result.
    std::vector<Diagnostic> toolWarnings;

    for (int pass = 0; pass < m_maxPasses; ++pass) {
        auto before = takeSnapshot(tracker);
        if (!before) {
            result.lastError = "snapshot failed: " + before.error().message();
            return result;
        }

        notify({ .kind = BuildEvent::Kind::PassStarted, .pass = pass + 1 });
        // Sources that exist before the pass; a file created during it
        // (epstopdf's figure-eps-converted-to.pdf) is generated, not edited.
        auto sourcesBefore = knownSources();
        auto passStart = fs::file_time_type::clock::now();
        auto engineResult = m_engine.run(m_inv, m_processLimits);
        ++result.passes;
        notify({ .kind = BuildEvent::Kind::PassFinished,
            .pass = pass + 1,
            .success = engineResult.success() });

        if (!engineResult.success()) {
            latexRule.markRan(m_db, false, command);
            result.lastError = failureMessage("latex", engineResult);
            result.diagnostics = diagnostics::extract(engineResult);
            return result;
        }

        // Wire up what the engine actually read this pass and hash it, so
        // the hashes recorded at convergence describe the compiled sources.
        if (auto fls = m_inv.jobFile(".fls"); fs::exists(fls))
            updateGraphFromFls(fls);
        for (auto* node : latexRule.inputs()) {
            if (auto r = node->refresh(tracker); !r) {
                result.lastError = "snapshot failed: " + r.error().message();
                return result;
            }
        }

        auto after = takeSnapshot(tracker);
        if (!after) {
            result.lastError = "snapshot failed: " + after.error().message();
            return result;
        }
        // A rerun is needed when a file the engine reads back (role
        // Generated, e.g. .aux, .toc) changed under the pass. Write-only
        // outputs (.idx, .bcf) are the tools' business. Only files that
        // existed before the pass can have changed; one written for the
        // first time is caught by the log (a rerun warning, or "No file …"
        // for something now present).
        bool readBackChanged = std::ranges::any_of(m_graph.files(), [&](const FileNode* f) {
            if (f->role() != FileRole::Generated)
                return false;
            auto was = before->find(f->path().string());
            auto now = after->find(f->path().string());
            return was != before->end() && (now == after->end() || now->second != was->second);
        });

        auto ctx = makeToolContext();
        auto tools = runStaleTools(ctx, tracker);
        if (tools.failure || tools.snapshotError) {
            latexRule.markRan(m_db, false, command);
            toolFailure(tools);
            return result;
        }
        std::ranges::move(tools.diagnostics, std::back_inserter(toolWarnings));

        bool rerun = ctx.log().rerunNeeded || readBackChanged
            || missingInputAppeared(ctx.log()) || sourceEditedDuringPass(sourcesBefore, passStart)
            || tools.outputsChanged;
        if (!rerun) {
            latexRule.markRan(m_db, true, command);
            persistFileStates();
            result.success = true;
            result.diagnostics = diagnostics::extract(engineResult);
            std::ranges::move(toolWarnings, std::back_inserter(result.diagnostics));
            return result;
        }
    }

    // A non-converged PDF must not count as current: the next invocation
    // has to rebuild rather than report "up to date".
    latexRule.markRan(m_db, false, command);
    result.lastError = "did not converge after "
        + std::to_string(m_maxPasses) + " passes";
    return result;
}

} // namespace maketex
