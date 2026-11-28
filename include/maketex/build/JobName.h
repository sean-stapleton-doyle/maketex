#pragma once

#include "maketex/EngineArguments.h"

#include <optional>
#include <string>
#include <utility>

namespace maketex::build {

// Splits a user-supplied -jobname out of the engine's extra options:
// the job name decides where the .fls, .log, .aux, and tool files are,
// so it must reach EngineInvocation rather than pass through verbatim.
// Accepts -jobname=x, --jobname=x, and -jobname x.
std::pair<EngineArguments, std::optional<std::string>> splitJobName(
    EngineArguments arguments);

} // namespace maketex::build
