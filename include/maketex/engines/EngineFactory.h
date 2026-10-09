#pragma once

#include "maketex/EngineArguments.h"
#include "maketex/EngineBase.h"

#include <memory>
#include <optional>
#include <string_view>

namespace maketex {

// The engines maketex can drive. Distinct from InvokerKind, which also tags
// auxiliary tools and exists for diagnostic-parser dispatch.
enum class EngineKind {
    PdfLaTeX,
    XeLaTeX,
    LuaLaTeX,
};

// Maps a config/CLI engine name ("pdflatex", "xelatex", "lualatex") to its
// kind, or nullopt for unknown names — callers own the error reporting.
std::optional<EngineKind> parseEngineKind(std::string_view name);

// Constructs the engine for kind. Total: every EngineKind yields an engine.
std::unique_ptr<EngineBase> createEngine(EngineKind kind,
    EngineArguments arguments = { });

} // namespace maketex
