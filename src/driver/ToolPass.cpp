#include "maketex/driver/ToolPass.h"

#include "maketex/DiagnosticExtractor.h"
#include "maketex/Launch.h"
#include "maketex/driver/ToolWaves.h"
#include "maketex/tools/InputLocator.h"

#include <algorithm>
#include <ctime>
#include <iterator>
#include <thread>
#include <utility>

namespace maketex::driver {

namespace fs = std::filesystem;

std::string toolRuleName(std::string_view toolName, std::string_view jobName)
{
    return "tool:" + std::string(toolName) + ":" + std::string(jobName);
}

ToolInvocation makeToolInvocation(const EngineInvocation& inv, const ITool& tool)
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

ToolPass::ToolPass(Deps deps, const EngineInvocation& inv)
    : m_deps(std::move(deps))
    , m_inv(inv)
{
}

void ToolPass::notify(const BuildEvent& event) const
{
    if (m_deps.observer)
        m_deps.observer(event);
}

std::int64_t ToolPass::nowEpoch() const
{
    return m_deps.clock ? m_deps.clock() : static_cast<std::int64_t>(std::time(nullptr));
}

std::expected<bool, std::error_code> ToolPass::isStale(const ITool& tool, const Digests& inputs)
{
    auto ruleName = toolRuleName(tool.name(), m_inv.resolvedJobName());
    auto record = m_deps.db.getRule(ruleName);
    if (!record || !record->succeeded)
        return true;

    Digests recorded;
    for (auto& [path, hash] : m_deps.db.getRuleInputs(ruleName))
        recorded.emplace(std::move(path), hash);
    if (recorded != inputs)
        return true;

    // The products of that run must still be what it wrote: a deleted or
    // hand-edited .bbl is regenerated.
    for (const auto& [path, hash] : m_deps.db.getRuleInputs(ruleName + "#outputs")) {
        auto current = m_deps.digests.currentHash(path);
        if (!current)
            return std::unexpected(current.error());
        if (!*current || **current != hash)
            return true;
    }
    return false;
}

std::expected<std::vector<ToolPass::PendingTool>, std::error_code> ToolPass::planStale(
    std::span<ITool* const> tools, const ToolContext& ctx)
{
    std::vector<PendingTool> pending;
    for (auto* tool : tools) {
        if (!tool->applies(ctx))
            continue;
        auto rawInputs = tool->inputs(ctx);
        auto inputs = m_deps.digests.digestInputs(rawInputs);
        if (!inputs)
            return std::unexpected(inputs.error());
        auto stale = isStale(*tool, *inputs);
        if (!stale)
            return std::unexpected(stale.error());
        if (!*stale)
            continue;

        std::unordered_set<std::string> inputKeys;
        for (const auto& ti : rawInputs)
            inputKeys.insert(FileDigests::canonicalKey(ti.path));

        auto outputs = tool->outputs(ctx);
        std::vector<std::optional<Hash>> before;
        std::unordered_set<std::string> outputKeys;
        for (const auto& out : outputs) {
            auto hash = m_deps.digests.currentHash(out);
            if (!hash)
                return std::unexpected(hash.error());
            before.push_back(*hash);
            outputKeys.insert(FileDigests::canonicalKey(out));
        }

        auto toolInv = makeToolInvocation(m_inv, *tool);
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
    return pending;
}

std::expected<void, std::error_code> ToolPass::refreshWaveInputs(
    std::span<PendingTool> pending, std::span<const std::size_t> wave)
{
    for (std::size_t idx : wave) {
        auto fresh = m_deps.digests.digestInputs(pending[idx].rawInputs);
        if (!fresh)
            return std::unexpected(fresh.error());
        pending[idx].inputs = std::move(*fresh);
    }
    return { };
}

std::vector<RunResult<ToolInvocation>> ToolPass::executeWave(
    std::span<const PendingTool> pending,
    std::span<const std::size_t> wave,
    const std::vector<std::pair<std::string, std::string>>& toolEnv)
{
    std::vector<RunResult<ToolInvocation>> results;
    results.reserve(wave.size());

    if (wave.size() == 1) {
        const auto& item = pending[wave.front()];
        notify({ .kind = BuildEvent::Kind::ToolStarted,
            .toolName = std::string(item.tool->name()) });
        results.push_back(launchProcess(m_deps.runner, item.args, item.inv.workingDir,
            item.tool->invokerKind(), item.inv, toolEnv, m_deps.limits));
        notify({ .kind = BuildEvent::Kind::ToolFinished,
            .toolName = std::string(item.tool->name()),
            .success = results.back().success() });
        return results;
    }

    for (std::size_t idx : wave)
        notify({ .kind = BuildEvent::Kind::ToolStarted,
            .toolName = std::string(pending[idx].tool->name()) });

    std::vector<std::optional<RunResult<ToolInvocation>>> waveResults(wave.size());
    {
        std::vector<std::jthread> workers;
        workers.reserve(wave.size());
        for (std::size_t i = 0; i < wave.size(); ++i) {
            const std::size_t idx = wave[i];
            workers.emplace_back([&, i, idx] {
                const auto& p = pending[idx];
                waveResults[i] = launchProcess(m_deps.runner, p.args, p.inv.workingDir,
                    p.tool->invokerKind(), p.inv, toolEnv, m_deps.limits);
            });
        }
    }
    for (std::size_t i = 0; i < wave.size(); ++i) {
        results.push_back(std::move(*waveResults[i]));
        notify({ .kind = BuildEvent::Kind::ToolFinished,
            .toolName = std::string(pending[wave[i]].tool->name()),
            .success = results.back().success() });
    }
    return results;
}

void ToolPass::recordFailure(const PendingTool& item,
    RunResult<ToolInvocation> toolResult,
    ToolPassOutcome& outcome)
{
    auto ruleName = toolRuleName(item.tool->name(), m_inv.resolvedJobName());
    m_deps.db.upsertRule(RuleRecord {
        .name = ruleName,
        .command = std::string(item.tool->name()),
        .ran_at = nowEpoch(),
        .succeeded = false,
    });
    if (!outcome.failure) {
        outcome.failedToolName = std::string(item.tool->name());
        outcome.failure = std::move(toolResult);
    }
}

std::expected<void, std::error_code> ToolPass::recordSuccess(PendingTool& item,
    const RunResult<ToolInvocation>& toolResult,
    ToolPassOutcome& outcome)
{
    std::vector<std::pair<std::string, Hash>> producedOutputs;
    bool changed = item.outputs.empty();
    for (std::size_t j = 0; j < item.outputs.size(); ++j) {
        auto after = m_deps.digests.currentHash(item.outputs[j]);
        if (!after)
            return std::unexpected(after.error());
        if (*after != item.before[j])
            changed = true;
        if (*after)
            producedOutputs.emplace_back(FileDigests::canonicalKey(item.outputs[j]), **after);
    }

    auto ruleName = toolRuleName(item.tool->name(), m_inv.resolvedJobName());
    auto now = nowEpoch();
    const std::string command(item.tool->name());
    m_deps.db.recordRun(
        { .name = ruleName + "#outputs", .command = command, .ran_at = now, .succeeded = true },
        producedOutputs);
    m_deps.db.recordRun(
        { .name = ruleName, .command = command, .ran_at = now, .succeeded = true },
        { item.inputs.begin(), item.inputs.end() });

    outcome.ranAny = true;
    outcome.outputsChanged = outcome.outputsChanged || changed;
    if (changed) {
        outcome.changes.push_back(item.outputs.empty()
                ? std::string(item.tool->name()) + " ran (its outputs are not tracked)"
                : std::string(item.tool->name()) + " rewrote "
                    + item.outputs.front().filename().string());
    }
    std::ranges::move(diagnostics::extract(toolResult), std::back_inserter(outcome.diagnostics));
    return { };
}

std::expected<void, std::error_code> ToolPass::recordWaveOutcomes(
    std::span<PendingTool> pending,
    std::span<const std::size_t> wave,
    std::vector<RunResult<ToolInvocation>>& results,
    ToolPassOutcome& outcome)
{
    for (std::size_t i = 0; i < wave.size(); ++i) {
        auto& item = pending[wave[i]];
        auto& toolResult = results[i];
        if (!toolResult.success()) {
            recordFailure(item, std::move(toolResult), outcome);
            continue;
        }
        if (auto r = recordSuccess(item, toolResult, outcome); !r)
            return std::unexpected(r.error());
    }
    return { };
}

ToolPassOutcome ToolPass::run(std::span<ITool* const> tools, const ToolContext& ctx)
{
    ToolPassOutcome outcome;
    auto pending = planStale(tools, ctx);
    if (!pending) {
        outcome.snapshotError = pending.error();
        return outcome;
    }
    if (pending->empty())
        return outcome;

    std::vector<ToolFootprint> footprints;
    footprints.reserve(pending->size());
    for (const auto& p : *pending) {
        footprints.push_back(ToolFootprint {
            .inputKeys = p.inputKeys,
            .outputKeys = p.outputKeys,
            .outputsUnknown = p.outputs.empty(),
        });
    }

    const auto toolEnv = toolSearchEnv(m_inv.texFile.parent_path(), m_inv.extraEnv);
    const auto waves = planToolWaves(footprints);
    for (std::size_t waveIdx = 0; waveIdx < waves.size(); ++waveIdx) {
        const auto& wave = waves[waveIdx];
        if (waveIdx > 0) {
            if (auto r = refreshWaveInputs(*pending, wave); !r) {
                outcome.snapshotError = r.error();
                return outcome;
            }
        }
        auto results = executeWave(*pending, wave, toolEnv);
        if (auto r = recordWaveOutcomes(*pending, wave, results, outcome); !r) {
            outcome.snapshotError = r.error();
            return outcome;
        }
        if (outcome.failure)
            return outcome;
    }
    return outcome;
}

} // namespace maketex::driver
