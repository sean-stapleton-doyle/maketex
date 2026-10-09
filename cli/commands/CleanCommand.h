#pragma once

#include "EffectiveOptions.h"

namespace maketex::cli {

// Removes generated files. Returns the process exit code.
int runCleanCommand(const EffectiveOptions& options);

} // namespace maketex::cli
