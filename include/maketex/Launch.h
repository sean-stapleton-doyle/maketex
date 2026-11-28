#pragma once

#include "maketex/Process.h"
#include "maketex/ProcessRunner.h"
#include "maketex/RunResult.h"

#include <cerrno>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace maketex {

// Launches argv as a subprocess from workDir, blocks until it exits (or
// limits cut it short), and wraps the outcome in a RunResult tagged with
// kind. Shared by EngineBase and BuildDriver's tool execution — the
// engine/tool only describes the command line; this owns process mechanics.
// A program that is not installed yields notFound (exit code 127, as a
// shell reports it) instead of an exception, so it surfaces as a diagnostic.
template <typename Inv>
RunResult<Inv> launchProcess(const ProcessRunner& runner,
    const std::vector<std::string>& args,
    const std::filesystem::path& workDir,
    InvokerKind kind,
    Inv invocation,
    std::span<const std::pair<std::string, std::string>> extraEnv = { },
    const WaitLimits& limits = { })
{
    RunResult<Inv> result {
        .invocation = std::move(invocation),
        .kind = kind,
        .program = args.empty() ? std::string { } : args.front(),
    };
    try {
        ProcessLaunchSpec spec {
            .argv = args,
            .workingDir = workDir,
            .extraEnv = { extraEnv.begin(), extraEnv.end() },
            .limits = limits,
        };
        auto pr = runner ? runner(spec) : runSystemProcess(spec);
        result.exitCode = pr.exitCode;
        result.stdoutOutput = std::move(pr.stdoutOutput);
        result.stderrOutput = std::move(pr.stderrOutput);
        result.timedOut = pr.timedOut;
        result.cancelled = pr.cancelled;
    } catch (const std::system_error& e) {
        // posix_spawnp reports ENOENT both for a missing program and a
        // missing working directory; only the former is "not installed".
        std::error_code ec;
        bool workDirOk = workDir.empty() || std::filesystem::is_directory(workDir, ec);
        if (e.code().value() != ENOENT || !workDirOk)
            throw;
        result.exitCode = 127;
        result.notFound = true;
    }
    return result;
}

template <typename Inv>
RunResult<Inv> launchProcess(const std::vector<std::string>& args,
    const std::filesystem::path& workDir,
    InvokerKind kind,
    Inv invocation,
    std::span<const std::pair<std::string, std::string>> extraEnv = { },
    const WaitLimits& limits = { })
{
    return launchProcess(ProcessRunner { }, args, workDir, kind,
        std::move(invocation), extraEnv, limits);
}

} // namespace maketex
