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

    // The effective TeX job name: explicit jobName if set, else the tex
    // file's stem. Determines the basename of .fls/.log/.aux/.bcf/… files.
    std::string resolvedJobName() const
    {
        return jobName.value_or(texFile.stem().string());
    }
};

} // namespace maketex
