#include "tools/BibtexTool.h"

#include "parsers/AuxParser.h"

namespace maketex {

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
