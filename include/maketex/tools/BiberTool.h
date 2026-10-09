#pragma once

#include "maketex/ITool.h"

namespace maketex {

// Runs biber for biblatex bibliographies.
// Applies when the latest pass wrote <jobName>.bcf.
// Inputs: the .bcf and every file datasource it lists (\addbibresource).
// Output: <outputDir>/<jobName>.bbl
class BiberTool final : public ITool {
public:
    std::string_view name() const override { return "biber"; }
    InvokerKind invokerKind() const override { return InvokerKind::Biber; }

    // argv: { "biber", "<jobName>" }, run from the output directory.
    std::vector<std::string> buildArgs(const ToolInvocation& inv) const override;

    bool applies(const ToolContext& ctx) const override;
    std::vector<ToolInput> inputs(const ToolContext& ctx) const override;
    std::vector<std::filesystem::path> outputs(const ToolContext& ctx) const override;
};

} // namespace maketex
