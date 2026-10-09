#include "maketex/tools/BiberTool.h"

#include "maketex/EngineInvocation.h"
#include "maketex/parsers/BcfParser.h"
#include "maketex/tools/InputLocator.h"

namespace maketex {

std::vector<std::string> BiberTool::buildArgs(const ToolInvocation& inv) const
{
    return { "biber", inv.jobName };
}

bool BiberTool::applies(const ToolContext& ctx) const
{
    return ctx.wrote(ctx.inv().jobFile(".bcf"));
}

std::vector<ToolInput> BiberTool::inputs(const ToolContext& ctx) const
{
    const auto& inv = ctx.inv();
    auto bcf = inv.jobFile(".bcf");

    std::vector<ToolInput> inputs;
    if (!std::filesystem::exists(bcf))
        return inputs;
    inputs.push_back({ .path = bcf });
    // Latex never reads the .bib files of a biblatex document, so they are
    // not in the .fls: without tracking them here an edited .bib would leave
    // the build "up to date".
    if (auto rec = BcfParser { }.parse(bcf)) {
        for (const auto& name : rec->dataSources)
            if (auto bib = locateToolInput(name, ".bib", inv, "bib"))
                inputs.push_back({ .path = *bib });
    }
    return inputs;
}

std::vector<std::filesystem::path> BiberTool::outputs(const ToolContext& ctx) const
{
    return { ctx.inv().jobFile(".bbl") };
}

} // namespace maketex
