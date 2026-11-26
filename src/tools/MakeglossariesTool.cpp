#include "maketex/tools/MakeglossariesTool.h"

#include "maketex/EngineInvocation.h"
#include "maketex/parsers/AuxParser.h"

#include <algorithm>
#include <filesystem>

namespace maketex {

namespace fs = std::filesystem;

namespace {

    std::string withDot(std::string_view ext)
    {
        if (ext.empty() || ext.starts_with('.'))
            return std::string(ext);
        return "." + std::string(ext);
    }

    std::vector<std::string> triggerExtensions(const ToolContext& ctx)
    {
        std::vector<std::string> exts { ".glo", ".acn" };
        for (const auto& g : ctx.aux().glossaries) {
            auto ext = withDot(g.triggerExt);
            if (!ext.empty() && !std::ranges::contains(exts, ext))
                exts.push_back(std::move(ext));
        }
        return exts;
    }

    std::vector<std::string> outputExtensions(const ToolContext& ctx)
    {
        std::vector<std::string> exts { ".gls", ".acr" };
        for (const auto& g : ctx.aux().glossaries) {
            auto ext = withDot(g.outputExt);
            if (!ext.empty() && !std::ranges::contains(exts, ext))
                exts.push_back(std::move(ext));
        }
        return exts;
    }

} // namespace

std::vector<std::string> MakeglossariesTool::buildArgs(const ToolInvocation& inv) const
{
    return { "makeglossaries", inv.jobName };
}

bool MakeglossariesTool::applies(const ToolContext& ctx) const
{
    for (const auto& ext : triggerExtensions(ctx))
        if (ctx.wrote(ctx.inv().jobFile(ext)))
            return true;
    return false;
}

std::vector<ToolInput> MakeglossariesTool::inputs(const ToolContext& ctx) const
{
    std::vector<ToolInput> result;
    std::error_code ec;
    for (const auto& ext : triggerExtensions(ctx)) {
        auto trigger = ctx.inv().jobFile(ext);
        if (fs::exists(trigger, ec))
            result.push_back({ .path = std::move(trigger) });
    }
    return result;
}

std::vector<fs::path> MakeglossariesTool::outputs(const ToolContext& ctx) const
{
    std::vector<fs::path> result;
    for (const auto& ext : outputExtensions(ctx))
        result.push_back(ctx.inv().jobFile(ext));
    return result;
}

} // namespace maketex
