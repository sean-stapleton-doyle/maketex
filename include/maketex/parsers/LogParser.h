#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace maketex {

// Signals extracted from a LaTeX .log file relevant to fixed-point scheduling.
// Error/warning surfacing for the user is DiagnosticExtractor's job, parsed
// from the captured stdout instead.
//
// The engine runs with max_print_line=10000 (EngineBase), so each message
// sits on one physical line and needs no unwrapping here.
struct LogRecord {
    // A package or the kernel asked for another pass ("Rerun to get …",
    // "Please rerun LaTeX", longtable's "Rerun LaTeX.", …).
    bool rerunNeeded = false;
    // Files LaTeX tried to read but did not find ("No file main.toc."), as
    // written in the log — usually a bare name relative to the output or
    // document directory. If one exists after the pass, the next pass reads
    // it and its output can change.
    std::vector<std::string> missingFiles;
};

class LogParser {
public:
    std::expected<LogRecord, std::error_code> parse(
        const std::filesystem::path& path) const;
};

} // namespace maketex
