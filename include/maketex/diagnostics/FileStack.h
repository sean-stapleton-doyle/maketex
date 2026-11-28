#pragma once

#include "maketex/diagnostics/SourceResolver.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace maketex::diagnostics {

// A file reference recovered from engine output: the display form for
// Diagnostic::file and, when the file exists, the resolved absolute path for
// Diagnostic::sourcePath.
struct SourceRef {
    std::string display;
    std::filesystem::path abs;
};

bool hasTexSourceExtension(const std::filesystem::path& p);

// Display form of a path token: relative to workingDir when underneath it,
// lexically normalised otherwise.
std::string displayFor(const std::filesystem::path& p,
    const std::filesystem::path& workingDir, const ISourceResolver& resolver);

// Resolves a path token printed by the engine against workingDir. Returns
// nullopt when the token does not name an existing file.
std::optional<SourceRef> resolveToken(std::string_view token,
    const std::filesystem::path& workingDir, const ISourceResolver& resolver);

SourceRef makeRootRef(const std::filesystem::path& texFile,
    const ISourceResolver& resolver);

// Builds the SourceRef for a file token from an error marker: display is the
// normalised token, abs only when the file exists.
SourceRef refForToken(std::string_view token,
    const std::filesystem::path& workingDir, const ISourceResolver& resolver);

// Lines where TeX echoes the user's source after an error: "l.42 \\foo{(}"
// and the indented remainder on the next line.
bool isSourceEcho(std::span<const std::string> lines, std::size_t index);

// Tracks TeX's paren-based open-file reporting: `(` followed by a path pushes
// a file, a bare `)` pops. Non-file parens push a sentinel so matching `)`
// pops the sentinel rather than a real file.
class FileStack {
public:
    FileStack(std::filesystem::path workingDir,
        const std::filesystem::path& texFile, const ISourceResolver& resolver);

    void scan(std::string_view line);

    // The innermost open file; the root document when nothing resolved.
    const SourceRef& current() const;
    const SourceRef& root() const { return m_root; }

private:
    std::filesystem::path m_workingDir;
    const ISourceResolver& m_resolver;
    SourceRef m_root;
    std::vector<std::optional<SourceRef>> m_stack;
};

} // namespace maketex::diagnostics
