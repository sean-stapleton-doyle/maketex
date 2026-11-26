#include "maketex/tools/Bib2glsTool.h"

#include "maketex/EngineInvocation.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/tools/InputLocator.h"

namespace maketex {

std::vector<std::string> Bib2glsTool::buildArgs(const ToolInvocation& inv) const
{
    if (!inv.outputDir.empty() && inv.outputDir != inv.workingDir)
        return { "bib2gls", "-d", inv.outputDir.string(), inv.jobName };
    return { "bib2gls", inv.jobName };
}

bool Bib2glsTool::applies(const ToolContext& ctx) const
{
    return !ctx.aux().glsOutputs.empty();
}

std::vector<ToolInput> Bib2glsTool::inputs(const ToolContext& ctx) const
{
    const auto& inv = ctx.inv();
    const auto& aux = ctx.aux();

    std::vector<ToolInput> result;
    auto signatureKey = inv.jobFile(".aux");
    signatureKey += "#bib2gls";
    result.push_back({ .path = std::move(signatureKey), .content = aux.bib2glsSignature });

    for (const auto& name : aux.glsBibFiles)
        if (auto bib = locateToolInput(name, ".bib", inv, "bib"))
            result.push_back({ .path = *bib });
    return result;
}

std::vector<std::filesystem::path> Bib2glsTool::outputs(const ToolContext& ctx) const
{
    std::vector<std::filesystem::path> result;
    for (const auto& base : ctx.aux().glsOutputs)
        result.push_back(ctx.inv().outputDir / (base + ".glstex"));
    return result;
}

} // namespace maketex
