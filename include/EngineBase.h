#pragma once

#include "EngineInvocation.h"
#include "IEngine.h"
#include "RunResult.h"

#include <string>
#include <vector>

namespace maketex {

// Concrete base for all latex-family engines.
// Subclasses implement buildArgs(); this class handles process launch and
// stdout/stderr capture.
class EngineBase : public IEngine {
public:
    RunResult<EngineInvocation> run(const EngineInvocation& inv) override;

    // Returns the full argv for the engine invocation.
    // argv[0] is looked up via PATH.
    virtual std::vector<std::string> buildArgs(const EngineInvocation& inv) const = 0;
};

} // namespace maketex
