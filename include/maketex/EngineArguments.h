#pragma once

#include <string>
#include <vector>

namespace maketex {

// Per-engine process configuration, eventually sourced from the user's
// config file. Complements EngineInvocation: this describes *how* to call
// the engine binary, EngineInvocation describes *what* to compile.
struct EngineArguments {
    // Executable to spawn, looked up via PATH. Empty means the engine's
    // default binary name ("pdflatex", "xelatex", ...).
    std::string program;

    // User-supplied options passed through verbatim, after the engine's
    // own flags and before the tex file.
    std::vector<std::string> extraOptions;
};

} // namespace maketex
