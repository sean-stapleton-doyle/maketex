#pragma once

#include "maketex/InvokerKind.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace maketex {

class IFileHasher;
class ITool;

// The auxiliary tools maketex can run between engine passes.
enum class ToolKind {
    Biber,
    Bibtex,
    Makeglossaries,
    Makeindex,
    Pythontex,
};

// Maps a config/CLI tool name ("biber", "makeindex", ...) to its kind, or
// nullopt for unknown names — callers own the error reporting.
std::optional<ToolKind> parseToolKind(std::string_view name);

// Constructs ITool instances, holding shared dependencies (the file hasher).
// Returned tools are owned by the caller via unique_ptr.
class ToolFactory {
public:
    // hasher is non-owning — caller must ensure it outlives the tools.
    explicit ToolFactory(IFileHasher& hasher);

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

    // Constructs the tool for kind with its standard recipe (trigger file,
    // command line, run-once behavior). Total: every ToolKind yields a tool.
    std::unique_ptr<ITool> create(ToolKind kind);

private:
    IFileHasher& m_hasher;
};

} // namespace maketex
