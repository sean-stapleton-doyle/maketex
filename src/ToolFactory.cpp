#include "maketex/ToolFactory.h"

#include "maketex/tools/BibtexTool.h"
#include "maketex/tools/HashTriggeredTool.h"

#include <utility>

namespace maketex {

ToolFactory::ToolFactory(IFileHasher& hasher)
    : m_hasher(hasher)
{
}

std::unique_ptr<ITool> ToolFactory::hashTriggered(
    std::string name,
    std::string triggerExtension,
    std::vector<std::string> argv,
    InvokerKind kind,
    bool runOnce)
{
    return std::make_unique<HashTriggeredTool>(
        std::move(name), std::move(triggerExtension), std::move(argv),
        kind, m_hasher, runOnce);
}

std::unique_ptr<ITool> ToolFactory::bibtex()
{
    return std::make_unique<BibtexTool>();
}

std::optional<ToolKind> parseToolKind(std::string_view name)
{
    if (name == "biber")
        return ToolKind::Biber;
    if (name == "bibtex")
        return ToolKind::Bibtex;
    if (name == "makeglossaries")
        return ToolKind::Makeglossaries;
    if (name == "makeindex")
        return ToolKind::Makeindex;
    if (name == "pythontex")
        return ToolKind::Pythontex;
    return std::nullopt;
}

std::unique_ptr<ITool> ToolFactory::create(ToolKind kind)
{
    switch (kind) {
    case ToolKind::Biber:
        return hashTriggered("biber", ".bcf", { "biber", "$job" },
            InvokerKind::Biber, /*runOnce=*/true);
    case ToolKind::Bibtex:
        return bibtex();
    case ToolKind::Makeglossaries:
        return hashTriggered("makeglossaries", ".glo",
            { "makeglossaries", "$job" }, InvokerKind::Makeglossaries);
    case ToolKind::Makeindex:
        return hashTriggered("makeindex", ".idx", { "makeindex", "$job" },
            InvokerKind::Makeindex);
    case ToolKind::Pythontex:
        return hashTriggered("pythontex", ".pytxcode", { "pythontex", "$job" },
            InvokerKind::Pythontex);
    }
    std::unreachable();
}

} // namespace maketex
