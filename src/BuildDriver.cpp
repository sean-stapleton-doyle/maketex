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
#include <format>
#include <iterator>
#include <thread>
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
    , m_contentFilters(std::move(config.contentFilters))
    , m_diagnosticPolicy(std::move(config.diagnosticPolicy))
    , m_preambleProvider(std::move(config.preambleProvider))
    , m_draftFirstPass(config.draftFirstPass)
    , m_xelatexXdvSplit(config.xelatexXdvSplit)
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

    ToolInvocation makeToolInv(const EngineInvocation& inv, const ITool& tool)
    {
        fs::path sourceDir = inv.texFile.parent_path();
        fs::path workingDir = tool.workingDirPreference() == ToolWorkingDir::Source
            ? sourceDir
            : inv.outputDir;
        return ToolInvocation {
            .workingDir = std::move(workingDir),
            .jobName = inv.resolvedJobName(),
            .outputDir = inv.outputDir,
            .sourceDir = std::move(sourceDir),
        };
    }

    // Files the engine writes for the user, for maketex, or for other tools,
    // never content a following pass depends on: excluded from convergence
    // snapshots (hashing a large PDF every pass would be pure cost). The
    // .run.xml is logreq's request list for external build tools; biblatex
    // flips its "active" flags from pass to pass.
    bool isFinalProduct(const fs::path& p)
    {
        auto name = p.filename().string();
        for (std::string_view ext : { ".pdf", ".dvi", ".xdv", ".log", ".fls",
                 ".synctex.gz", ".synctex", ".run.xml" })
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

    // Filters ignored non-error diagnostics and promotes warnings to errors
    // according to policy. Returns an error summary if the policy failed the
    // build, or empty when the build still passes.
    std::string applyDiagnosticPolicy(std::vector<Diagnostic>& diagnostics,
        const DiagnosticPolicy& policy)
    {
        if (!policy.ignore.empty()) {
            std::erase_if(diagnostics, [&](const Diagnostic& d) {
                return d.severity != Diagnostic::Severity::Error
                    && !d.category.empty()
                    && std::ranges::contains(policy.ignore, d.category);
            });
        }

        int promoted = 0;
        std::string firstCategory;
        for (auto& d : diagnostics) {
            if (d.severity != Diagnostic::Severity::Warning)
                continue;
            bool fail = policy.warningsAsErrors
                || (!d.category.empty() && std::ranges::contains(policy.failOn, d.category));
            if (fail) {
                d.severity = Diagnostic::Severity::Error;
                if (firstCategory.empty())
                    firstCategory = d.category;
                ++promoted;
            }
        }
        if (promoted == 0)
            return { };
        if (policy.warningsAsErrors)
            return std::format("{} warning{} treated as error{}",
                promoted, promoted == 1 ? "" : "s", promoted == 1 ? "" : "s");
        return std::format("failed on {} warning{}", firstCategory, promoted == 1 ? "" : "s");
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
    return projectFlsParser(m_inv.texFile.parent_path(), m_ignorePrefixes);
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

    // The .fls is the complete record of one pass: an \input the user
    // removed must stop being a dependency (and stop being persisted).
    auto& latexRule = m_graph.getOrAddRule(engineRuleName());
    latexRule.clearEdges();

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
    // A read-back file counts only if it existed when the build was
    // recorded: minted writes, reads, and deletes main.pyg in every pass.
    for (auto* node : latexRule->outputs()) {
        bool needed = isFinalProduct(node->path())
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
    const auto toolEnv = toolSearchEnv(m_inv.texFile.parent_path(), m_inv.extraEnv);

    struct PendingTool {
        ITool* tool = nullptr;
        std::vector<ToolInput> rawInputs;
        Digests inputs;
        std::unordered_set<std::string> inputKeys;
        std::vector<fs::path> outputs;
        std::vector<std::optional<Hash>> before;
        std::unordered_set<std::string> outputKeys;
        std::vector<std::string> args;
        ToolInvocation inv;
    };

    ToolPassOutcome outcome;
    std::vector<PendingTool> pending;
    for (auto* tool : m_tools) {
        if (!tool->applies(ctx))
            continue;
        auto rawInputs = tool->inputs(ctx);
        auto inputs = digestInputs(rawInputs, tracker);
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

        std::unordered_set<std::string> inputKeys;
        for (const auto& ti : rawInputs)
            inputKeys.insert(key(ti.path));

        auto outputs = tool->outputs(ctx);
        std::vector<std::optional<Hash>> before;
        std::unordered_set<std::string> outputKeys;
        for (const auto& out : outputs) {
            auto hash = currentHash(out, tracker);
            if (!hash) {
                outcome.snapshotError = hash.error();
                return outcome;
            }
            before.push_back(*hash);
            outputKeys.insert(key(out));
        }

        auto toolInv = makeToolInv(m_inv, *tool);
        auto args = tool->buildArgs(toolInv);
        pending.push_back(PendingTool {
            .tool = tool,
            .rawInputs = std::move(rawInputs),
            .inputs = std::move(*inputs),
            .inputKeys = std::move(inputKeys),
            .outputs = std::move(outputs),
            .before = std::move(before),
            .outputKeys = std::move(outputKeys),
            .args = std::move(args),
            .inv = std::move(toolInv),
        });
    }

    if (pending.empty())
        return outcome;

    // Group stale tools into waves that can safely execute in parallel: two
    // tools conflict if either has unknown outputs, or if they share an output
    // path or one's output is the other's declared input (even if that input
    // does not exist yet before the earlier wave runs).
    auto conflicts = [](const PendingTool& a, const PendingTool& b) {
        if (a.outputs.empty() || b.outputs.empty())
            return true;
        for (const auto& out : a.outputKeys)
            if (b.outputKeys.contains(out) || b.inputKeys.contains(out))
                return true;
        for (const auto& out : b.outputKeys)
            if (a.inputKeys.contains(out))
                return true;
        return false;
    };

    std::vector<std::vector<size_t>> waves;
    for (size_t idx = 0; idx < pending.size(); ++idx) {
        if (!waves.empty()) {
            bool canJoin = std::ranges::none_of(waves.back(), [&](size_t prev) {
                return conflicts(pending[prev], pending[idx]);
            });
            if (canJoin) {
                waves.back().push_back(idx);
                continue;
            }
        }
        waves.push_back({ idx });
    }

    for (size_t waveIdx = 0; waveIdx < waves.size(); ++waveIdx) {
        const auto& wave = waves[waveIdx];
        if (waveIdx > 0) {
            // Re-digest declared inputs for subsequent waves so outputs
            // produced by an earlier wave are recorded accurately in stateDb.
            for (size_t idx : wave) {
                auto fresh = digestInputs(pending[idx].rawInputs, tracker);
                if (!fresh) {
                    outcome.snapshotError = fresh.error();
                    return outcome;
                }
                pending[idx].inputs = std::move(*fresh);
            }
        }
        std::vector<RunResult<ToolInvocation>> results;
        results.reserve(wave.size());

        if (wave.size() == 1) {
            auto& item = pending[wave.front()];
            notify({ .kind = BuildEvent::Kind::ToolStarted,
                .toolName = std::string(item.tool->name()) });
            auto workingDir = item.inv.workingDir;
            results.push_back(launchProcess(item.args, workingDir, item.tool->invokerKind(),
                item.inv, toolEnv, m_processLimits));
            notify({ .kind = BuildEvent::Kind::ToolFinished,
                .toolName = std::string(item.tool->name()),
                .success = results.back().success() });
        } else {
            for (size_t idx : wave)
                notify({ .kind = BuildEvent::Kind::ToolStarted,
                    .toolName = std::string(pending[idx].tool->name()) });

            std::vector<std::optional<RunResult<ToolInvocation>>> waveResults(wave.size());
            {
                std::vector<std::jthread> workers;
                workers.reserve(wave.size());
                for (size_t i = 0; i < wave.size(); ++i) {
                    const size_t idx = wave[i];
                    workers.emplace_back([&, i, idx] {
                        const auto& p = pending[idx];
                        waveResults[i] = launchProcess(p.args, p.inv.workingDir,
                            p.tool->invokerKind(), p.inv, toolEnv, m_processLimits);
                    });
                }
            }
            for (size_t i = 0; i < wave.size(); ++i) {
                results.push_back(std::move(*waveResults[i]));
                notify({ .kind = BuildEvent::Kind::ToolFinished,
                    .toolName = std::string(pending[wave[i]].tool->name()),
                    .success = results.back().success() });
            }
        }

        // Record outcomes deterministically in roster order on the main thread.
        for (size_t i = 0; i < wave.size(); ++i) {
            auto& item = pending[wave[i]];
            auto& toolResult = results[i];
            auto ruleName = toolRuleName(*item.tool);
            if (!toolResult.success()) {
                recordRule(m_db, ruleName, false, std::string(item.tool->name()));
                if (!outcome.failure) {
                    outcome.failedToolName = std::string(item.tool->name());
                    outcome.failure = std::move(toolResult);
                }
                continue;
            }

            std::vector<std::pair<std::string, Hash>> producedOutputs;
            bool changed = item.outputs.empty();
            for (size_t j = 0; j < item.outputs.size(); ++j) {
                auto after = currentHash(item.outputs[j], tracker);
                if (!after) {
                    outcome.snapshotError = after.error();
                    return outcome;
                }
                if (*after != item.before[j])
                    changed = true;
                if (*after)
                    producedOutputs.emplace_back(key(item.outputs[j]), **after);
            }
            auto now = static_cast<int64_t>(std::time(nullptr));
            const std::string command(item.tool->name());
            m_db.recordRun({ .name = ruleName + "#outputs", .command = command, .ran_at = now, .succeeded = true },
                producedOutputs);
            m_db.recordRun({ .name = ruleName, .command = command, .ran_at = now, .succeeded = true },
                { item.inputs.begin(), item.inputs.end() });

            outcome.ranAny = true;
            outcome.outputsChanged = outcome.outputsChanged || changed;
            if (changed) {
                outcome.changes.push_back(item.outputs.empty()
                        ? std::string(item.tool->name()) + " ran (its outputs are not tracked)"
                        : std::string(item.tool->name()) + " rewrote " + item.outputs.front().filename().string());
            }
            std::ranges::move(diagnostics::extract(toolResult), std::back_inserter(outcome.diagnostics));
        }

        if (outcome.failure)
            return outcome;
    }
    return outcome;
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
    for (const auto& [extension, pattern] : m_contentFilters)
        tracker.setIgnorePattern(extension, pattern);

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
            if (auto policyError = applyDiagnosticPolicy(result.diagnostics, m_diagnosticPolicy);
                !policyError.empty()) {
                result.lastError = std::move(policyError);
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
    }

    auto& latexRule = m_graph.getOrAddRule(engineRuleName());
    const auto command = m_inv.texFile.string();
    // Tool warnings collected across passes; reported with the result.
    std::vector<Diagnostic> toolWarnings;

    if (m_inv.preamble.empty() && m_preambleProvider)
        m_inv.preamble = m_preambleProvider();

    for (int pass = 0; pass < m_maxPasses; ++pass) {
        auto before = takeSnapshot(tracker);
        if (!before) {
            result.lastError = "snapshot failed: " + before.error().message();
            return result;
        }

        notify({ .kind = BuildEvent::Kind::PassStarted, .pass = pass + 1 });
        // Sources that exist before the pass; a file created during it
        // (epstopdf's figure-eps-converted-to.pdf) is generated, not edited.
        auto sourcesBefore = stampKnownSources();
        EngineInvocation passInv = m_inv;
        const bool ranDraft = (pass == 0 && m_draftFirstPass) || m_xelatexXdvSplit;
        passInv.draftMode = ranDraft;
        auto engineResult = m_engine.run(passInv, m_processLimits);
        // Checked now, before tools rewrite their outputs (.bbl is a source
        // the engine reads): only the user's saves during the pass count.
        auto edited = editedDuringPass(sourcesBefore);
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
        auto files = m_graph.files();
        auto readBackChanged = std::ranges::find_if(files, [&](const FileNode* f) {
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

        std::vector<std::string> reasons;
        if (ctx.log().rerunNeeded)
            reasons.emplace_back("LaTeX asked for a rerun");
        if (readBackChanged != files.end())
            reasons.push_back((*readBackChanged)->path().filename().string() + " changed");
        if (auto appeared = appearedInput(ctx.log()))
            reasons.push_back(*appeared);
        if (edited)
            reasons.push_back(edited->filename().string() + " was saved during the pass");
        reasons.insert(reasons.end(), tools.changes.begin(), tools.changes.end());
        // If pass 1 ran with -draftmode (pdflatex/lualatex) and unexpectedly
        // produced no rerun signals, run one non-draft pass to emit the PDF.
        if (reasons.empty() && pass == 0 && m_draftFirstPass && !m_xelatexXdvSplit)
            reasons.emplace_back("emitting final PDF after draft pass");
        if (!reasons.empty()) {
            std::string why;
            for (const auto& r : reasons)
                why += (why.empty() ? "" : "; ") + r;
            result.rerunReasons.push_back(std::format("pass {}: {}", pass + 2, why));
            notify({ .kind = BuildEvent::Kind::RerunScheduled, .pass = pass + 2, .detail = why });
        }
        if (reasons.empty()) {
            // XeLaTeX -no-pdf split: convert the converged .xdv to .pdf once.
            if (m_xelatexXdvSplit) {
                auto xdv = m_inv.jobFile(".xdv");
                if (fs::exists(xdv)) {
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
                    auto xdvRes = launchProcess(xdvArgs, m_inv.outputDir, InvokerKind::Xdvipdfmx,
                        std::move(xdvInv), xdvEnv, m_processLimits);
                    notify({ .kind = BuildEvent::Kind::ToolFinished,
                        .toolName = "xdvipdfmx",
                        .success = xdvRes.success() });
                    if (!xdvRes.success()) {
                        latexRule.markRan(m_db, false, command);
                        result.lastError = failureMessage("xdvipdfmx", xdvRes);
                        result.diagnostics = diagnostics::extract(xdvRes);
                        return result;
                    }
                    std::ranges::move(diagnostics::extract(xdvRes), std::back_inserter(toolWarnings));
                }
            }

            result.diagnostics = diagnostics::extract(engineResult);
            std::ranges::move(toolWarnings, std::back_inserter(result.diagnostics));
            if (auto policyError = applyDiagnosticPolicy(result.diagnostics, m_diagnosticPolicy);
                !policyError.empty()) {
                // Failed by diagnostic policy: don't record as succeeded so
                // the next invocation does not skip as "up to date".
                latexRule.markRan(m_db, false, command);
                persistFileStates();
                result.lastError = std::move(policyError);
                result.success = false;
                return result;
            }
            latexRule.markRan(m_db, true, command);
            persistFileStates();
            result.success = true;
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
