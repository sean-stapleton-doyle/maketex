#pragma once

#include "maketex/ITool.h"

namespace maketex {

// Runs bibtex for classic BibTeX bibliography processing.
// Applies when the .aux (or a child .aux from an \include) has \bibdata.
// Inputs: the \citation/\bibdata/\bibstyle lines of the .aux tree (not the
//         whole file — LaTeX rewrites that every pass), the .bib databases
//         wherever bibtex will find them, and document-local .bst styles.
// Output: <outputDir>/<jobName>.bbl
class BibtexTool final : public ITool {
public:
    std::string_view name() const override { return "bibtex"; }
    InvokerKind invokerKind() const override { return InvokerKind::Bibtex; }

    // argv: { "bibtex", "<jobName>" }
    // Runs from workingDir (= outputDir). TeX Live openout_any=p rejects
    // absolute paths for output files; bare job name avoids this.
    std::vector<std::string> buildArgs(const ToolInvocation& inv) const override;

    bool applies(const ToolContext& ctx) const override;
    std::vector<ToolInput> inputs(const ToolContext& ctx) const override;
    std::vector<std::filesystem::path> outputs(const ToolContext& ctx) const override;
};

} // namespace maketex
