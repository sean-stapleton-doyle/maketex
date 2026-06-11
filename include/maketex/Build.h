#pragma once

#include "maketex/BuildDriver.h"
#include "maketex/EngineArguments.h"
#include "maketex/ToolFactory.h"
#include "maketex/engines/EngineFactory.h"

#include <filesystem>
#include <vector>

namespace maketex {

// TeX distribution roots (TEXMFROOT, system/user caches), resolved via
// kpsewhich. Files under these are excluded from dependency tracking — the
// engine reads hundreds of .sty/.cls/font files per pass, and treating them
// as inputs would mean hashing the whole distribution closure every build.
// Returns an empty list (track everything) when kpsewhich is unavailable.
std::vector<std::filesystem::path> texSystemRoots();

// Everything needed to run a complete build. The eventual CLI constructs
// this from flags and the user's config file; apps/main.cpp constructs it
// from flags only.
struct BuildOptions {
    std::filesystem::path texFile; // absolute path to the root .tex file

    EngineKind engine = EngineKind::PdfLatex;
    EngineArguments engineArguments;

    // Auxiliary tools eligible to run between engine passes, in this order.
    std::vector<ToolKind> tools = {
        ToolKind::Biber,
        ToolKind::Makeindex,
        ToolKind::Makeglossaries,
        ToolKind::Pythontex,
        ToolKind::Bibtex,
    };

    // Where the engine writes its outputs and where .maketex.db lives.
    // Empty means the tex file's directory.
    std::filesystem::path outputDir;

    bool haltOnError = true;
    bool shellEscape = false;
    bool synctex = false;
    int maxPasses = BuildDriver::kDefaultMaxPasses;
};

// Assembles the full build — engine, state database, hasher, and the default
// auxiliary tool roster (biber, bibtex, makeindex, makeglossaries, pythontex)
// — and drives it to convergence. Environment failures (engine not
// installed, database errors) propagate as exceptions from the underlying
// components.
DriverResult runBuild(const BuildOptions& options);

} // namespace maketex
