#pragma once

#include <string>

namespace maketex {

struct Diagnostic {
    enum class Severity { Error, Warning, Info };

    Severity severity = Severity::Error;
    std::string file;       // source .tex file; empty if unknown
    int line = -1;          // 1-based source line; -1 if unknown
    std::string message;    // clean, human-readable message
    std::string rawContext; // surrounding log lines; shown only in verbose mode
};

} // namespace maketex
