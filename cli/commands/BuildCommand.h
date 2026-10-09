#pragma once

#include "EffectiveOptions.h"

namespace maketex::cli {

// One-shot build. Returns the process exit code.
int runBuildCommand(const EffectiveOptions& options);

} // namespace maketex::cli
