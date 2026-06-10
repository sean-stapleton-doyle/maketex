#pragma once

#include "ITool.h"

namespace maketex {

// Runs bibtex for classic BibTeX bibliography processing.
// Input:  <outputDir>/<jobName>.aux (must contain \bibdata and \citation entries)
//         .bib and .bst files found via TEXINPUTS
// Output: <outputDir>/<jobName>.bbl, <outputDir>/<jobName>.blg
class BibtexTool final : public ITool {
public:
    std::string_view name() const override { return "bibtex"; }
    InvokerKind invokerKind() const override { return InvokerKind::Bibtex; }
    RunDecision decide(const ToolContext& ctx) override;
    void onSuccess(const ToolContext& ctx) override;

    // The .aux file plus every \bibdata database resolvable next to the
    // document or in the output directory. .bib files found only via
    // TEXINPUTS elsewhere are not tracked — edits to those won't invalidate
    // the incremental skip.
    std::vector<std::filesystem::path> trackedInputs(
        const ToolContext& ctx) const override;

    // argv: { "bibtex", "<jobName>" }
    // Runs from workingDir (= outputDir). TeX Live openout_any=p rejects
    // absolute paths for output files; bare job name avoids this.
    std::vector<std::string> buildArgs(const ToolInvocation& inv) const override;

private:
    bool m_hasRun = false;
};

} // namespace maketex
