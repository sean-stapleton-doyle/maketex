#include "ToolFactory.h"

#include "tools/BibtexTool.h"
#include "tools/HashTriggeredTool.h"

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

} // namespace maketex
