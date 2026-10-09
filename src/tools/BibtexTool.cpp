#include "maketex/tools/BibtexTool.h"

#include "maketex/EngineInvocation.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/tools/InputLocator.h"

namespace maketex {

std::vector<std::string> BibtexTool::buildArgs(const ToolInvocation& inv) const
{
    return { "bibtex", inv.jobName };
}

bool BibtexTool::applies(const ToolContext& ctx) const
{
    return !ctx.aux().bibDataFiles.empty();
}

std::vector<ToolInput> BibtexTool::inputs(const ToolContext& ctx) const
{
    const auto& inv = ctx.inv();
    const auto& aux = ctx.aux();

    std::vector<ToolInput> inputs;
    auto signatureKey = inv.jobFile(".aux");
    signatureKey += "#bibtex";
    inputs.push_back({ .path = signatureKey, .content = aux.bibtexSignature });

    for (const auto& name : aux.bibDataFiles)
        if (auto bib = locateToolInput(name, ".bib", inv, "bib"))
            inputs.push_back({ .path = *bib });
    // A changed standard style arrives with a TeX Live update, which the
    // build fingerprint does not see either; only document-local styles
    // are tracked, which spares a kpsewhich call per build.
    for (const auto& name : aux.bibStyles)
        if (auto bst = locateToolInput(name, ".bst", inv, std::nullopt))
            inputs.push_back({ .path = *bst });
    return inputs;
}

std::vector<std::filesystem::path> BibtexTool::outputs(const ToolContext& ctx) const
{
    return { ctx.inv().jobFile(".bbl") };
}

} // namespace maketex
