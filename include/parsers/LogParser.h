#pragma once

#include <expected>
#include <filesystem>
#include <system_error>

namespace maketex {

// Signals extracted from a LaTeX .log file relevant to fixed-point scheduling.
// Error surfacing (file/line/severity) is a Stage 2 concern and not included here.
//
// Note: LaTeX wraps log lines at 79 characters by default. The patterns matched
// here are short enough to fit on a single line; multi-line warning detection is
// not implemented.
struct LogRecord {
    bool rerunNeeded = false;         // any "Rerun to get …" or "Please rerun LaTeX"
    bool bibtexRerunNeeded = false;   // "run BibTeX" warning (secondary; primary trigger is AuxParser)
    bool undefinedReferences = false; // "There were undefined references"
    bool undefinedCitations = false;  // Citation '…' undefined on any line
};

class LogParser {
public:
    std::expected<LogRecord, std::error_code> parse(
        const std::filesystem::path& path) const;
};

} // namespace maketex
