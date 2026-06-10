#include "tools/BibtexTool.h"

#include "EngineInvocation.h"
#include "parsers/AuxParser.h"

#include <filesystem>

namespace maketex {

namespace fs = std::filesystem;

std::vector<fs::path> BibtexTool::trackedInputs(const ToolContext& ctx) const
{
    const auto& inv = ctx.inv();
    std::vector<fs::path> inputs;

    auto aux = inv.outputDir / (inv.resolvedJobName() + ".aux");
    if (fs::exists(aux))
        inputs.push_back(aux);

    for (const auto& name : ctx.aux().bibDataFiles) {
        fs::path bib(name);
        if (bib.extension().empty())
            bib.replace_extension(".bib");
        for (const auto& dir : { inv.texFile.parent_path(), inv.outputDir }) {
            auto candidate = bib.is_absolute() ? bib : dir / bib;
            if (fs::exists(candidate)) {
                inputs.push_back(candidate);
                break;
            }
        }
    }
    return inputs;
}

RunDecision BibtexTool::decide(const ToolContext& ctx)
{
    if (m_hasRun)
        return RunDecision::Done;
    if (ctx.aux().bibDataFiles.empty())
        return RunDecision::Skip;
    return RunDecision::Run;
}

void BibtexTool::onSuccess(const ToolContext& /*ctx*/)
{
    m_hasRun = true;
}

std::vector<std::string> BibtexTool::buildArgs(const ToolInvocation& inv) const
{
    // bibtex is run from outputDir (via workingDir in ToolInvocation) and
    // takes just the job name — TeX Live's openout_any=p security restriction
    // rejects absolute paths when writing .blg outside the working directory.
    return { "bibtex", inv.jobName };
}

} // namespace maketex
