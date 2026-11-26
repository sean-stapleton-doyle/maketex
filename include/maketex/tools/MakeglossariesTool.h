#pragma once

#include "maketex/ITool.h"

namespace maketex {

// Runs makeglossaries for standard (.glo/.acn → .gls/.acr) and any custom
// glossaries registered in the .aux via \@newglossary{name}{log}{out}{in}.
class MakeglossariesTool final : public ITool {
public:
    std::string_view name() const override { return "makeglossaries"; }
    InvokerKind invokerKind() const override { return InvokerKind::Makeglossaries; }

    std::vector<std::string> buildArgs(const ToolInvocation& inv) const override;
    bool applies(const ToolContext& ctx) const override;
    std::vector<ToolInput> inputs(const ToolContext& ctx) const override;
    std::vector<std::filesystem::path> outputs(const ToolContext& ctx) const override;
};

} // namespace maketex
