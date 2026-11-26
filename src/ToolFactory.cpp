#include "maketex/ToolFactory.h"

#include "maketex/tools/Bib2glsTool.h"
#include "maketex/tools/BiberTool.h"
#include "maketex/tools/BibtexTool.h"
#include "maketex/tools/HashTriggeredTool.h"
#include "maketex/tools/MakeglossariesTool.h"

#include <utility>

namespace maketex {

std::optional<ToolKind> parseToolKind(std::string_view name)
{
    if (name == "biber")
        return ToolKind::Biber;
    if (name == "bib2gls")
        return ToolKind::Bib2gls;
    if (name == "bibtex")
        return ToolKind::Bibtex;
    if (name == "makeglossaries")
        return ToolKind::Makeglossaries;
    if (name == "makeindex")
        return ToolKind::Makeindex;
    if (name == "nomencl")
        return ToolKind::Nomencl;
    if (name == "pythontex")
        return ToolKind::Pythontex;
    return std::nullopt;
}

std::unique_ptr<ITool> createTool(ToolKind kind)
{
    switch (kind) {
    case ToolKind::Biber:
        return std::make_unique<BiberTool>();
    case ToolKind::Bib2gls:
        return std::make_unique<Bib2glsTool>();
    case ToolKind::Bibtex:
        return std::make_unique<BibtexTool>();
    case ToolKind::Makeglossaries:
        return std::make_unique<MakeglossariesTool>();
    case ToolKind::Makeindex:
        return std::make_unique<HashTriggeredTool>(HashTriggeredTool::Recipe {
            .name = "makeindex",
            .kind = InvokerKind::Makeindex,
            .argv = { "makeindex", "$job" },
            .triggerExtensions = { ".idx" },
            .outputExtensions = { ".ind" },
        });
    case ToolKind::Nomencl:
        return std::make_unique<HashTriggeredTool>(HashTriggeredTool::Recipe {
            .name = "nomencl",
            .kind = InvokerKind::Makeindex,
            .argv = { "makeindex", "$job.nlo", "-s", "nomencl.ist", "-o", "$job.nls" },
            .triggerExtensions = { ".nlo" },
            .outputExtensions = { ".nls" },
        });
    case ToolKind::Pythontex:
        // pythontex writes into pythontex-files-<job>/ in its cwd (outputDir);
        // which of those files the engine reads back depends on the document,
        // so outputs stay unknown and every pythontex run is followed by an
        // engine pass.
        return std::make_unique<HashTriggeredTool>(HashTriggeredTool::Recipe {
            .name = "pythontex",
            .kind = InvokerKind::Pythontex,
            .argv = { "pythontex", "$job" },
            .triggerExtensions = { ".pytxcode" },
        });
    }
    std::unreachable();
}

} // namespace maketex
