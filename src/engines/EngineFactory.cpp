#include "maketex/engines/EngineFactory.h"

#include "maketex/engines/LuaLatexEngine.h"
#include "maketex/engines/PdfLatexEngine.h"
#include "maketex/engines/XeLatexEngine.h"

#include <utility>

namespace maketex {

std::optional<EngineKind> parseEngineKind(std::string_view name)
{
    if (name == "pdflatex")
        return EngineKind::PdfLatex;
    if (name == "xelatex")
        return EngineKind::XeLatex;
    if (name == "lualatex")
        return EngineKind::LuaLatex;
    return std::nullopt;
}

std::unique_ptr<EngineBase> createEngine(EngineKind kind,
    EngineArguments arguments)
{
    switch (kind) {
    case EngineKind::PdfLatex:
        return std::make_unique<PdfLatexEngine>(std::move(arguments));
    case EngineKind::XeLatex:
        return std::make_unique<XeLatexEngine>(std::move(arguments));
    case EngineKind::LuaLatex:
        return std::make_unique<LuaLatexEngine>(std::move(arguments));
    }
    std::unreachable();
}

} // namespace maketex
