#pragma once

#include <filesystem>
#include <string>

namespace maketex {

struct Diagnostic {
    enum class Severity { Error,
        Warning,
        Info };

    Severity severity = Severity::Error;
    std::string file; // display path relative to the tex dir; empty if unknown
    std::filesystem::path sourcePath; // absolute resolved path; empty if unresolved
    int line = -1; // 1-based source line; -1 if unknown
    std::string message; // clean, human-readable message
    std::string category; // stable machine tag ("undefined-citation",
                          // "missing-file", "package", …); empty if untagged
    std::string rawContext; // surrounding log lines; shown only in verbose mode
};

} // namespace maketex
