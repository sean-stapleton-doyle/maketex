#include "ToolRunner.h"

#include "Process.h"

#include <memory>
#include <utility>

namespace maketex {

ToolRunner::ToolRunner(tf::Executor& executor)
    : m_executor(executor)
{
}

std::future<RunResult<ToolInvocation>> ToolRunner::run(ITool& tool, ToolInvocation inv)
{
    auto args = tool.buildArgs(inv);
    auto workDir = inv.workingDir;
    auto invPtr = std::make_shared<const ToolInvocation>(std::move(inv));
    auto kind = tool.invokerKind();

    return m_executor.async([args = std::move(args),
                                workDir = std::move(workDir),
                                invPtr = std::move(invPtr),
                                kind]() mutable {
        auto pr = Process(args, workDir).wait();
        return RunResult<ToolInvocation> {
            .invocation = std::move(invPtr),
            .kind = kind,
            .exitCode = pr.exitCode,
            .stdoutOutput = std::move(pr.stdoutOutput),
            .stderrOutput = std::move(pr.stderrOutput),
        };
    });
}

} // namespace maketex
