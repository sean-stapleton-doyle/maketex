#pragma once

#include "maketex/Process.h"
#include "maketex/RunResult.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace maketex {

// Launches argv as a subprocess from workDir, blocks until it exits, and
// wraps the outcome in a RunResult tagged with kind. Shared by EngineBase
// and BuildDriver's tool execution — the engine/tool only describes the
// command line; this owns process mechanics.
template <typename Inv>
RunResult<Inv> launchProcess(const std::vector<std::string>& args,
    const std::filesystem::path& workDir,
    InvokerKind kind,
    Inv invocation)
{
    auto pr = Process(args, workDir).wait();
    return RunResult<Inv> {
        .invocation = std::move(invocation),
        .kind = kind,
        .exitCode = pr.exitCode,
        .stdoutOutput = std::move(pr.stdoutOutput),
        .stderrOutput = std::move(pr.stderrOutput),
    };
}

} // namespace maketex
