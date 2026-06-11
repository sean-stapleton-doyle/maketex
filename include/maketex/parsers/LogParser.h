#pragma once

#include <expected>
#include <filesystem>
#include <system_error>

namespace maketex {

// Signals extracted from a LaTeX .log file relevant to fixed-point scheduling.
// Error/warning surfacing for the user is DiagnosticExtractor's job, parsed
// from the captured stdout instead.
//
// Note: LaTeX wraps log lines at 79 characters by default. The patterns matched
// here are short enough to fit on a single line; multi-line warning detection is
// not implemented.
struct LogRecord {
    bool rerunNeeded = false; // any "Rerun to get …" or "Please rerun LaTeX"
};

class LogParser {
public:
    std::expected<LogRecord, std::error_code> parse(
        const std::filesystem::path& path) const;
};

} // namespace maketex
