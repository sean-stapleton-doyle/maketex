#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace maketex {

struct EngineInvocation {
    std::filesystem::path texFile;
    std::filesystem::path outputDir;
    std::filesystem::path auxDir;
    std::optional<std::string> jobName;
    bool recorder = true;
    bool haltOnError = true;
    bool shellEscape = false;
    bool synctex = false;
};

} // namespace maketex
