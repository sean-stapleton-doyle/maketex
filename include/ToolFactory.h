#pragma once

#include "InvokerKind.h"

#include <memory>
#include <string>
#include <vector>

namespace maketex {

class Blake3Hasher;
class ITool;

// Constructs ITool instances, holding shared dependencies (the file hasher).
// Returned tools are owned by the caller via unique_ptr.
class ToolFactory {
public:
    // hasher is non-owning — caller must ensure it outlives the tools.
    explicit ToolFactory(Blake3Hasher& hasher);

    // A tool that fires when <jobName><triggerExtension>'s hash changes.
    // "$job" in argv is replaced with the resolved job name at run time.
    // runOnce: when true, the tool transitions to Done after first success.
    std::unique_ptr<ITool> hashTriggered(
        std::string name,
        std::string triggerExtension,
        std::vector<std::string> argv,
        InvokerKind kind,
        bool runOnce = false);

    // Classic BibTeX. Fires when the .aux file lists \bibdata entries.
    std::unique_ptr<ITool> bibtex();

private:
    Blake3Hasher& m_hasher;
};

} // namespace maketex
