#pragma once

#include "maketex/ITool.h"

namespace maketex {

// Runs bib2gls for glossaries-extra documents using \GlsXtrLoadResources.
// Applies when the .aux contains \glsxtr@resource lines.
// Runs from sourceDir with `-d <outputDir>` so relative .bib paths resolve.
class Bib2glsTool final : public ITool {
public:
    std::string_view name() const override { return "bib2gls"; }
    InvokerKind invokerKind() const override { return InvokerKind::Bib2gls; }
    ToolWorkingDir workingDirPreference() const override { return ToolWorkingDir::Source; }

    std::vector<std::string> buildArgs(const ToolInvocation& inv) const override;
    bool applies(const ToolContext& ctx) const override;
    std::vector<ToolInput> inputs(const ToolContext& ctx) const override;
    std::vector<std::filesystem::path> outputs(const ToolContext& ctx) const override;
};

} // namespace maketex
