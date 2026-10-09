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
    // For "missing-file" / "missing-tool": the file or program that was not
    // found (e.g. "minted.sty", "biber") — what a frontend looks up to
    // suggest an install command.
    std::string missingFile;
    // An actionable next step, shown under the message ("rebuild with
    // --shell-escape", "sudo tlmgr install minted"). Empty when none.
    std::string hint;
};

} // namespace maketex
