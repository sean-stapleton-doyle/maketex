#include "EngineBase.h"

#include "Process.h"

#include <future>
#include <memory>

namespace maketex {

EngineBase::EngineBase(tf::Executor& executor)
    : m_executor(executor)
{
}

std::future<RunResult<EngineInvocation>> EngineBase::run(const EngineInvocation& inv)
{
    // Build argv and capture invocation context on the calling thread before
    // submitting — inv may be a temporary.
    // Run from the tex file's directory so that relative \input{} and \include{}
    // paths resolve correctly regardless of maketex's own working directory.
    auto args = buildArgs(inv);
    auto workDir = inv.texFile.parent_path();
    auto invPtr = std::make_shared<const EngineInvocation>(inv);
    auto kind = invokerKind();

    return m_executor.async([args = std::move(args),
                                workDir = std::move(workDir),
                                invPtr = std::move(invPtr),
                                kind]() mutable {
        auto pr = Process(args, workDir).wait();
        return RunResult<EngineInvocation> {
            .invocation = std::move(invPtr),
            .kind = kind,
            .exitCode = pr.exitCode,
            .stdoutOutput = std::move(pr.stdoutOutput),
            .stderrOutput = std::move(pr.stderrOutput),
        };
    });
}

} // namespace maketex
