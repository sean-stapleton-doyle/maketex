#pragma once

#include "maketex/Diagnostic.h"
#include "maketex/diagnostics/SourceResolver.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace maketex::diagnostics {

struct LineCursor {
    std::span<const std::string> lines;
    std::size_t index = 0;
};

struct WarningMatch {
    bool matched = false; // line(s) consumed
    std::optional<Diagnostic> diagnostic; // empty for skipped summary lines
};

using WarningMatcher = WarningMatch (*)(LineCursor&);

WarningMatch matchLatexWarning(LineCursor& cursor); // + indented continuations
WarningMatch matchNamedWarning(LineCursor& cursor); // Package/Class/Font + (name) continuations
WarningMatch matchMissingCharacter(LineCursor& cursor);
WarningMatch matchPdfTexWarning(LineCursor& cursor);
WarningMatch matchBoxWarning(LineCursor& cursor); // skips typeset content

std::vector<Diagnostic> extractLatexWarnings(const std::string& output,
    const std::filesystem::path& texFile, const ISourceResolver& resolver);

} // namespace maketex::diagnostics
