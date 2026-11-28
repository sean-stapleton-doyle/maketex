#pragma once

#include "maketex/Diagnostic.h"
#include "maketex/InvokerKind.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace maketex::diagnostics {

struct LinePattern {
    std::string_view token;
    bool prefixMatch; // starts_with vs. contains
    Diagnostic::Severity severity;
    bool messageAfterToken; // message = rest of line after token, else whole line
};

std::vector<Diagnostic> extractByLinePatterns(
    std::string_view output, std::span<const LinePattern> patterns);

std::vector<Diagnostic> extractBibtex(std::string_view output);

std::vector<Diagnostic> extractToolOutput(InvokerKind kind, const std::string& output);

} // namespace maketex::diagnostics
