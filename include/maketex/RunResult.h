#pragma once

#include "maketex/InvokerKind.h"

#include <string>

namespace maketex {

// Result of a single engine or tool invocation.
//
// `kind` is stamped by the launcher from the engine/tool's invokerKind() —
// it identifies who produced the output so DiagnosticExtractor can dispatch
// to the right log parser.
template <typename Inv>
struct RunResult {
    Inv invocation;
    InvokerKind kind;
    int exitCode = 0;
    std::string stdoutOutput;
    std::string stderrOutput;
    std::string program; // argv[0], for messages
    bool notFound = false; // argv[0] is not installed (not on PATH)
    bool timedOut = false; // killed after WaitLimits::timeout
    bool cancelled = false; // killed because the build was cancelled

    bool success() const { return exitCode == 0; }
};

} // namespace maketex
