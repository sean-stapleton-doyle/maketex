#include "maketex/tools/HashTriggeredTool.h"

#include "maketex/EngineInvocation.h"

#include <filesystem>
#include <utility>

namespace maketex {

namespace fs = std::filesystem;

HashTriggeredTool::HashTriggeredTool(Recipe recipe)
    : m_recipe(std::move(recipe))
{
}

std::vector<std::string> HashTriggeredTool::buildArgs(const ToolInvocation& inv) const
{
    auto argv = m_recipe.argv;
    for (auto& a : argv)
        if (a == "$job")
            a = inv.jobName;
    return argv;
}

bool HashTriggeredTool::applies(const ToolContext& ctx) const
{
    for (const auto& ext : m_recipe.triggerExtensions)
        if (ctx.wrote(ctx.inv().jobFile(ext)))
            return true;
    return false;
}

std::vector<ToolInput> HashTriggeredTool::inputs(const ToolContext& ctx) const
{
    std::vector<ToolInput> inputs;
    for (const auto& ext : m_recipe.triggerExtensions) {
        auto trigger = ctx.inv().jobFile(ext);
        if (fs::exists(trigger))
            inputs.push_back({ .path = std::move(trigger) });
    }
    return inputs;
}

std::vector<fs::path> HashTriggeredTool::outputs(const ToolContext& ctx) const
{
    std::vector<fs::path> outputs;
    for (const auto& ext : m_recipe.outputExtensions)
        outputs.push_back(ctx.inv().jobFile(ext));
    return outputs;
}

} // namespace maketex
