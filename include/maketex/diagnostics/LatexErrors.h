#pragma once

#include "maketex/Diagnostic.h"
#include "maketex/diagnostics/SourceResolver.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace maketex::diagnostics {

// A `file:line: message` error marker produced by -file-line-error.
struct FileLineError {
    std::string fileToken;
    int line = -1;
    std::string message;
};

struct ContextResult {
    std::string rawCtx;
    int line = -1; // line number found in the look-ahead, -1 if none
};

std::optional<FileLineError> parseFileLineError(std::string_view line,
    const std::filesystem::path& workingDir, const ISourceResolver& resolver);

ContextResult gatherContext(std::span<const std::string> lines, std::size_t index,
    const std::filesystem::path& workingDir, const ISourceResolver& resolver);

// Appends `(name) ...` continuation lines starting at lines[next] to message.
std::string foldContinuation(std::string message,
    std::span<const std::string> lines, std::size_t next);

// The 1-based line in `text` that loads `name` (a .sty via \usepackage or
// \RequirePackage, a .cls via \documentclass, anything else via \input or
// \include), or -1.
int loadingLine(std::string_view text, const std::filesystem::path& name);

void refineErrors(std::vector<Diagnostic>& diags,
    const std::filesystem::path& texFile, const ISourceResolver& resolver);

std::vector<Diagnostic> extractLatexErrors(const std::string& output,
    const std::filesystem::path& texFile, const ISourceResolver& resolver);

} // namespace maketex::diagnostics
