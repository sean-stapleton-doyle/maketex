#include "maketex/engines/EngineFactory.h"

#include "maketex/engines/LuaLatexEngine.h"
#include "maketex/engines/PdfLatexEngine.h"
#include "maketex/engines/XeLatexEngine.h"

#include <utility>

namespace maketex {

std::optional<EngineKind> parseEngineKind(std::string_view name)
{
    if (name == "pdflatex")
        return EngineKind::PdfLaTeX;
    if (name == "xelatex")
        return EngineKind::XeLaTeX;
    if (name == "lualatex")
        return EngineKind::LuaLaTeX;
    return std::nullopt;
}

std::unique_ptr<EngineBase> createEngine(EngineKind kind,
    EngineArguments arguments)
{
    switch (kind) {
    case EngineKind::PdfLaTeX:
        return std::make_unique<PdfLatexEngine>(std::move(arguments));
    case EngineKind::XeLaTeX:
        return std::make_unique<XeLatexEngine>(std::move(arguments));
    case EngineKind::LuaLaTeX:
        return std::make_unique<LuaLatexEngine>(std::move(arguments));
    }
    std::unreachable();
}

} // namespace maketex
