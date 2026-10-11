#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace maketex {

struct EngineInvocation {
    std::filesystem::path texFile;
    std::filesystem::path outputDir;
    std::optional<std::string> jobName;
    bool haltOnError = true;
    bool shellEscape = false;
    bool synctex = false;
    // TeX code run before the document, for package options the document
    // can't know it needs (e.g. minted 2's outputdir). Empty: the engine is
    // given the tex file directly.
    std::string preamble;

    // The effective TeX job name: explicit jobName if set, else the tex
    // file's stem. Determines the basename of .fls/.log/.aux/.bcf/… files.
    std::string resolvedJobName() const
    {
        return jobName.value_or(texFile.stem().string());
    }

    // <outputDir>/<job><extension>: where the engine and tools put the
    // job's .aux, .log, .bbl, … files.
    std::filesystem::path jobFile(std::string_view extension) const
    {
        auto name = resolvedJobName();
        name += extension;
        return outputDir / name;
    }
};

} // namespace maketex
