#pragma once

#include "maketex/Build.h"
#include "maketex/engines/EngineFactory.h"

#include <optional>

namespace maketex::build {

// Loads a previously remembered Unicode engine for options from .maketex.db,
// or nullopt if no database exists, no entry is stored, or reading fails.
std::optional<EngineKind> loadDetectedEngine(const BuildOptions& options);

// Best-effort persistence of a Unicode engine discovered via retry.
void saveDetectedEngine(const BuildOptions& options, EngineKind engine);

} // namespace maketex::build
