#pragma once

#include "maketex/Build.h"

#include <optional>

namespace maketex::build {

// Environment of one runBuild call: choosing an engine, performing a build
// attempt with a given engine, and remembering/cleaning up around the one-shot
// Unicode engine retry.
class IBuildAttempts {
public:
    virtual ~IBuildAttempts() = default;

    virtual EngineChoice chooseEngine(const BuildOptions& options) = 0;
    virtual BuildResult attempt(const BuildOptions& options,
        EngineKind engine,
        bool suppressResetNotice)
        = 0;
    virtual std::optional<EngineKind> rememberedEngine(const BuildOptions& options) = 0;
    virtual void rememberEngine(const BuildOptions& options, EngineKind engine) = 0;
    virtual void discardPartialAux(const BuildOptions& options) = 0;
};

// Orchestrates engine choice, remembered-engine reuse, and the one-shot
// Unicode engine retry when a guessed pdflatex build fails because the
// document needs LuaLaTeX or XeLaTeX.
BuildResult runBuild(const BuildOptions& options, IBuildAttempts& attempts);

} // namespace maketex::build
