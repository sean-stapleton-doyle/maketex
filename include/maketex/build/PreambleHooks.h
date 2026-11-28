#pragma once

#include "maketex/ProcessRunner.h"
#include "maketex/engines/EngineFactory.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace maketex::build {

// Extracts the major version number from the first 60 lines of minted.sty
// ("Yet another Pygments shim ... v<major>"), or nullopt when unparsable.
std::optional<int> parseMintedMajorVersion(std::string_view styHead);

// Returns the TeX preamble hook passing outputdir to minted v2.
std::string mintedOutputDirHook(std::string_view relOutDir);

// Returns the TikZ externalization system-call hook when source enables
// externalization without overriding `system call`, else empty.
std::string tikzExternalHook(std::string_view source,
    std::string_view engineBin, std::string_view relOutDir);

// Computes TeX preamble hooks needed for minted v2 and TikZ externalization
// when building into a separate output directory.
std::string buildPreambleHooks(const std::filesystem::path& texFile,
    const std::filesystem::path& outDir,
    EngineKind engineKind,
    std::string_view engineProgram,
    bool shellEscape,
    const ProcessRunner& runner = { });

} // namespace maketex::build
