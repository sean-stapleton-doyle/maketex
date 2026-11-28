#pragma once

#include "maketex/Build.h"
#include "maketex/IFileHasher.h"
#include "maketex/ProcessRunner.h"
#include "maketex/StateDatabase.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maketex::build {

// Extracts everything strictly before `\begin{document}` from texSource, or
// nullopt when unsuitable for precompiled format dumping (no \begin{document},
// empty preamble, a \begin{...} inside the preamble, or {minted}).
std::optional<std::string> extractDumpablePreambleText(std::string_view texSource);

// File wrapper around extractDumpablePreambleText.
std::optional<std::string> extractDumpablePreamble(const std::filesystem::path& texFile);

// Lowercase hex encoding of a BLAKE3 Hash.
std::string hexDigest(const Hash& h);

// Builds the deterministic key material hashed to validate a cached .fmt file.
std::string preambleCacheMaterial(std::string_view program,
    bool shellEscape,
    std::span<const std::string> extraOptions,
    std::span<const std::pair<std::string, std::string>> extraEnv,
    std::string_view preambleText,
    std::span<const std::pair<std::string, std::string>> inputDigests);

// Constructs the `pdflatex -ini` command line used to dump a preamble format.
std::vector<std::string> formatDumpArgs(std::string_view bin,
    const std::filesystem::path& outDir,
    std::string_view fmtName,
    bool shellEscape,
    const std::filesystem::path& preambleTex);

// Builds (or reuses) `<outDir>/<job>-preamble.fmt` for pdflatex. Returns the
// format name ("<job>-preamble") on success, or empty to fall back to a
// normal compilation. When systemRoots is nullopt, queries texSystemRoots().
std::string preparePreambleFormat(const BuildOptions& options,
    const EngineArguments& engineArguments,
    const std::filesystem::path& outDir,
    const std::string& resolvedJob,
    const std::vector<std::pair<std::string, std::string>>& extraEnv,
    StateDatabase& stateDb,
    IFileHasher& hasher,
    const ProcessRunner& runner = { },
    std::optional<std::span<const std::filesystem::path>> systemRoots = std::nullopt);

} // namespace maketex::build
