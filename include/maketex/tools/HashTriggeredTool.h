#pragma once

#include "maketex/ITool.h"

#include <string>
#include <vector>

namespace maketex {

// An auxiliary tool that turns engine-written trigger files into files the
// engine reads back: makeindex (.idx → .ind), makeglossaries (.glo/.acn →
// .gls/.acr), pythontex (.pytxcode → …).
//
// Applies when the latest engine pass wrote any trigger
// (<outputDir>/<jobName><ext>). Its inputs are the trigger files, so it
// reruns exactly when their content changes. Use "$job" as a placeholder in
// argv — it is replaced with the job name when buildArgs() is called.
class HashTriggeredTool final : public ITool {
public:
    struct Recipe {
        std::string name;
        InvokerKind kind = InvokerKind::Makeindex;
        std::vector<std::string> argv;
        std::vector<std::string> triggerExtensions;
        // Output extensions the engine reads back; empty when the outputs
        // are not known (see ITool::outputs).
        std::vector<std::string> outputExtensions;
    };

    explicit HashTriggeredTool(Recipe recipe);

    std::string_view name() const override { return m_recipe.name; }
    InvokerKind invokerKind() const override { return m_recipe.kind; }
    std::vector<std::string> buildArgs(const ToolInvocation& inv) const override;

    bool applies(const ToolContext& ctx) const override;
    std::vector<ToolInput> inputs(const ToolContext& ctx) const override;
    std::vector<std::filesystem::path> outputs(const ToolContext& ctx) const override;

private:
    Recipe m_recipe;
};

} // namespace maketex
