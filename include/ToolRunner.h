#pragma once

#include "ITool.h"
#include "RunResult.h"
#include "ToolInvocation.h"

#include <taskflow/taskflow.hpp>

#include <future>

namespace maketex {

// Executes an ITool against a ToolInvocation on a shared tf::Executor.
//
// Separating execution from the tool description (ITool) means tools are
// pure data + small overrides — no taskflow dependency leaks into them.
class ToolRunner {
public:
    // executor is non-owning — caller must ensure it outlives the runner.
    explicit ToolRunner(tf::Executor& executor);

    // Stamps tool.invokerKind() onto inv, builds argv, launches the process
    // asynchronously, and returns a future carrying the RunResult.
    std::future<RunResult<ToolInvocation>> run(ITool& tool, ToolInvocation inv);

private:
    tf::Executor& m_executor;
};

} // namespace maketex
