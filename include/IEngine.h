#pragma once

#include "EngineInvocation.h"
#include "InvokerKind.h"
#include "RunResult.h"

#include <string_view>

namespace maketex {

class IEngine {
public:
    virtual ~IEngine() = default;

    // Runs the engine to completion and returns the captured outcome.
    virtual RunResult<EngineInvocation> run(const EngineInvocation&) = 0;

    // Human-readable name, e.g. "pdflatex", "xelatex".
    virtual std::string_view name() const = 0;

    // Kind tag stamped onto RunResult before delivery — used by
    // DiagnosticExtractor to dispatch to the right log parser.
    virtual InvokerKind invokerKind() const = 0;
};

} // namespace maketex
