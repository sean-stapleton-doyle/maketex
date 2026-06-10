#pragma once

#include "InvokerKind.h"

#include <memory>
#include <string>

namespace maketex {

// Result of a single engine or tool invocation.
//
// `kind` is stamped by the runner (EngineBase / ToolRunner) from the
// engine/tool's invokerKind() — it identifies who produced the output so
// DiagnosticExtractor can dispatch to the right log parser.
template <typename Inv>
struct RunResult {
    std::shared_ptr<const Inv> invocation;
    InvokerKind kind;
    int exitCode = 0;
    std::string stdoutOutput;
    std::string stderrOutput;

    bool success() const { return exitCode == 0; }
};

} // namespace maketex
