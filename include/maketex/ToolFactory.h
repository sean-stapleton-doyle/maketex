#pragma once

#include <memory>
#include <optional>
#include <string_view>

namespace maketex {

class ITool;

// The auxiliary tools maketex can run between engine passes.
enum class ToolKind {
    Biber,
    Bib2gls,
    Bibtex,
    Makeglossaries,
    Makeindex,
    Nomencl,
    Pythontex,
};

// Maps a config/CLI tool name ("biber", "makeindex", ...) to its kind, or
// nullopt for unknown names — callers own the error reporting.
std::optional<ToolKind> parseToolKind(std::string_view name);

// Constructs the tool for kind with its standard recipe (trigger files,
// outputs, command line). Total: every ToolKind yields a tool.
std::unique_ptr<ITool> createTool(ToolKind kind);

} // namespace maketex
