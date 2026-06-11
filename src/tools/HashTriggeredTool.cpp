#include "maketex/tools/HashTriggeredTool.h"

#include "maketex/EngineInvocation.h"
#include "maketex/IFileHasher.h"

#include <filesystem>
#include <utility>

namespace maketex {

namespace fs = std::filesystem;

HashTriggeredTool::HashTriggeredTool(std::string name,
    std::string triggerExtension,
    std::vector<std::string> argv,
    InvokerKind kind,
    IFileHasher& hasher,
    bool runOnce)
    : m_name(std::move(name))
    , m_triggerExtension(std::move(triggerExtension))
    , m_argvTemplate(std::move(argv))
    , m_kind(kind)
    , m_hasher(hasher)
    , m_runOnce(runOnce)
{
}

std::vector<std::string> HashTriggeredTool::buildArgs(const ToolInvocation& inv) const
{
    auto argv = m_argvTemplate;
    for (auto& a : argv)
        if (a == "$job")
            a = inv.jobName;
    return argv;
}

fs::path HashTriggeredTool::triggerPath(const ToolContext& ctx) const
{
    const auto& inv = ctx.inv();
    return inv.outputDir / (inv.resolvedJobName() + m_triggerExtension);
}

std::vector<fs::path> HashTriggeredTool::trackedInputs(const ToolContext& ctx) const
{
    auto trigger = triggerPath(ctx);
    if (!fs::exists(trigger))
        return { };
    return { trigger };
}

RunDecision HashTriggeredTool::decide(const ToolContext& ctx)
{
    if (m_runOnce && m_hasRun)
        return RunDecision::Done;

    auto trigger = triggerPath(ctx);
    if (!fs::exists(trigger))
        return RunDecision::Skip;

    auto hashed = m_hasher.hashFile(trigger);
    if (!hashed.has_value())
        return RunDecision::Skip; // trigger file vanished or unreadable
    if (m_lastHash.has_value() && m_lastHash.value() == hashed.value())
        return RunDecision::Skip;

    m_pendingHash = hashed.value();
    return RunDecision::Run;
}

void HashTriggeredTool::onSuccess(const ToolContext& /*ctx*/)
{
    if (m_pendingHash)
        m_lastHash = std::move(m_pendingHash);
    m_pendingHash.reset();
    m_hasRun = true;
}

} // namespace maketex
