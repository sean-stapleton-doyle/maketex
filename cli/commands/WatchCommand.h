#pragma once

#include "EffectiveOptions.h"

namespace maketex::cli {

// Rebuilds on file change until interrupted. Full-screen TUI on a terminal,
// per-build inline reports with --no-tui or when stdout is not a tty.
// Returns the process exit code.
int runWatchCommand(const EffectiveOptions& options);

} // namespace maketex::cli
