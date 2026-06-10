#pragma once

#include "EngineInvocation.h"
#include "IEngine.h"
#include "RunResult.h"

#include <taskflow/taskflow.hpp>

#include <future>
#include <string>
#include <vector>

namespace maketex {

// Concrete base for all latex-family engines.
// Subclasses implement buildArgs(); this class handles process launch,
// stdout/stderr capture, and future delivery via a shared tf::Executor.
class EngineBase : public IEngine {
public:
    // executor is non-owning — caller must ensure it outlives the engine.
    explicit EngineBase(tf::Executor& executor);

    std::future<RunResult<EngineInvocation>> run(const EngineInvocation& inv) override;
    bool handlesOwnLoop() const override { return false; }

    // Returns the full argv for the engine invocation.
    // argv[0] is looked up via PATH. Called on the submitting thread.
    virtual std::vector<std::string> buildArgs(const EngineInvocation& inv) const = 0;

private:
    tf::Executor& m_executor;
};

} // namespace maketex
