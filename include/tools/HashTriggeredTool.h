#pragma once

#include "ITool.h"
#include "StateDatabase.h"

#include <optional>
#include <string>
#include <vector>

namespace maketex {

class Blake3Hasher;

// Auxiliary tool that runs whenever the hash of a trigger file changes.
//
// The trigger file is resolved as <outputDir>/<jobName><triggerExtension>;
// if it exists and its hash differs from the last successful run, the tool
// fires. Use "$job" as a placeholder in argv — it is replaced with the
// resolved job name when buildArgs() is called.
//
// runOnce: when true, transitions to Done after first success and will not
// fire again during the same build session. Use for tools whose input is
// rewritten by latex on every pass (e.g. biber's .bcf).
class HashTriggeredTool final : public ITool {
public:
    HashTriggeredTool(std::string name,
        std::string triggerExtension,
        std::vector<std::string> argv,
        InvokerKind kind,
        Blake3Hasher& hasher,
        bool runOnce = false);

    std::string_view name() const override { return m_name; }
    InvokerKind invokerKind() const override { return m_kind; }
    std::vector<std::string> buildArgs(const ToolInvocation& inv) const override;
    RunDecision decide(const ToolContext& ctx) override;
    void onSuccess(const ToolContext& ctx) override;

private:
    std::string m_name;
    std::string m_triggerExtension;
    std::vector<std::string> m_argvTemplate;
    InvokerKind m_kind;
    Blake3Hasher& m_hasher;
    bool m_runOnce;

    // Trigger state, updated across passes via decide()/onSuccess().
    std::optional<Hash> m_lastHash;
    std::optional<Hash> m_pendingHash;
    bool m_hasRun = false;
};

} // namespace maketex
