#pragma once

#include <filesystem>
#include <string>

namespace maketex {

// Invocation parameters for auxiliary tools (biber, bibtex, makeindex, etc.).
// Lighter than EngineInvocation — tools don't need recorder, synctex, etc.
struct ToolInvocation {
    std::filesystem::path workingDir;
    std::string jobName;
    std::filesystem::path outputDir;
};

} // namespace maketex
