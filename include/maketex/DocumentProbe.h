#pragma once

#include "maketex/engines/EngineFactory.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace maketex {

// Editor magic comments at the top of a .tex file (TeXShop, TeXstudio,
// VS Code LaTeX Workshop, and Overleaf all honour them):
//   % !TEX root = ../thesis.tex
//   % !TeX program = xelatex        (also "TS-program")
struct MagicComments {
    // The root document named by "!TEX root", resolved against the file's
    // directory. Not checked for existence.
    std::optional<std::filesystem::path> root;
    std::optional<EngineKind> program;
};

// Reads magic comments from the leading lines of texFile. Returns an empty
// record when the file is unreadable or carries none.
MagicComments readMagicComments(const std::filesystem::path& texFile);

// Follows "!TEX root" from texFile to the document it belongs to — what an
// editor passes when the user builds from a chapter file. Returns texFile
// itself when it names no root (or the chain is broken or cyclic).
std::filesystem::path resolveRootDocument(const std::filesystem::path& texFile);

// The .tex files directly in dir that start a document (an uncommented
// \documentclass near the top), sorted by name.
std::vector<std::filesystem::path> findRootDocuments(const std::filesystem::path& dir);

// The engine a document asks for, and why.
struct EngineChoice {
    EngineKind kind = EngineKind::PdfLaTeX;
    // Human-readable: "% !TeX program", "loads fontspec", … Empty for the
    // pdflatex default.
    std::string reason;
};

// Picks an engine from the document's source: a "!TeX program" comment,
// else the packages its preamble loads (following \input one level) —
// Lua-only packages or \directlua → lualatex; XeTeX-only packages
// (xeCJK, mathspec, …) → xelatex; other Unicode-engine packages (fontspec,
// unicode-math, polyglossia) → lualatex; otherwise pdflatex.
EngineChoice detectEngine(const std::filesystem::path& texFile);

// What building a document will take, read from its source (the root file
// plus the files it \input/\includes directly). Static analysis: it sees what
// the source says, not what packages load behind the scenes.
struct DocumentRequirements {
    std::string documentClass; // e.g. "article"; empty if not found
    std::vector<std::string> packages; // \usepackage / \RequirePackage names
    EngineChoice engine;
    // Programs the build will run besides the engine, each with the reason:
    // { "biber", "loads biblatex" }, { "pygmentize", "loads minted" }, …
    std::vector<std::pair<std::string, std::string>> programs;
    // The document runs external programs (minted, svg, …) and needs
    // --shell-escape.
    bool needsShellEscape = false;
};

DocumentRequirements analyzeRequirements(const std::filesystem::path& texFile);

} // namespace maketex
