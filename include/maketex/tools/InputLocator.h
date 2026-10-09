#pragma once

#include "maketex/EngineInvocation.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maketex {

// The kpathsea environment auxiliary tools run under. Tools run from the
// output directory (their .aux/.idx inputs live there), which hides
// source-adjacent files — .bib, .bst, .ist — once the output directory is
// not the tex directory. Each variable gets the tex directory prepended to
// the user's existing value; an unset variable becomes "<texdir>:", whose
// trailing ':' appends the system defaults (latexmk does the same with
// -outdir).
std::vector<std::pair<std::string, std::string>> toolSearchEnv(
    const std::filesystem::path& texDir);

// Finds the file a tool will read for `name` (as written in the document,
// e.g. "refs" or "refs.bib"), adding `extension` when name has none. Looks
// beside the document, then in the output directory, then — when
// `kpsewhichFormat` is set — asks kpsewhich under toolSearchEnv, which
// covers BIBINPUTS and the TeX tree. Returns nullopt when not found.
std::optional<std::filesystem::path> locateToolInput(std::string_view name,
    std::string_view extension,
    const EngineInvocation& inv,
    std::optional<std::string_view> kpsewhichFormat);

} // namespace maketex
