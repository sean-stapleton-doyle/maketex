#pragma once

#include "maketex/Diagnostic.h"
#include "maketex/EngineInvocation.h"
#include "maketex/RunResult.h"
#include "maketex/ToolInvocation.h"

#include <vector>

namespace maketex::diagnostics {

class ISourceResolver;

// Extracts structured Diagnostics from the captured stdout of an engine or
// tool invocation. Dispatches on result.kind to the appropriate parser.
//
// Always returns a non-empty vector on failure: if no structured diagnostics
// are found, a fallback Diagnostic wraps the raw stdout in rawContext so the
// caller never has to handle raw log output directly.
std::vector<Diagnostic> extract(const RunResult<EngineInvocation>& result);
std::vector<Diagnostic> extract(
    const RunResult<EngineInvocation>& result, const ISourceResolver& resolver);
std::vector<Diagnostic> extract(const RunResult<ToolInvocation>& result);

} // namespace maketex::diagnostics
