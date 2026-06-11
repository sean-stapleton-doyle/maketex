#pragma once

#include "EngineArguments.h"
#include "EngineInvocation.h"
#include "IEngine.h"
#include "RunResult.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maketex {

// Concrete base for all latex-family engines.
// Subclasses implement buildArgs(); this class handles process launch,
// stdout/stderr capture, and storage of the user's engine configuration.
// Flag assembly is deliberately *not* shared: engines beyond the TeX Live
// trio (tectonic, plain latex) won't speak the same command line.
class EngineBase : public IEngine {
public:
    EngineBase() = default;
    explicit EngineBase(EngineArguments arguments)
        : m_arguments(std::move(arguments))
    {
    }

    RunResult<EngineInvocation> run(const EngineInvocation& inv) override;

    // Returns the full argv for the engine invocation.
    // argv[0] is looked up via PATH.
    virtual std::vector<std::string> buildArgs(const EngineInvocation& inv) const = 0;

protected:
    const EngineArguments& arguments() const { return m_arguments; }

    // Resolved argv[0]: the user-configured program if set, else fallback.
    std::string programOr(std::string_view fallback) const
    {
        return m_arguments.program.empty() ? std::string(fallback)
                                           : m_arguments.program;
    }

    // Appends the user's extraOptions to args, dropping any whose option key
    // (text before '=') collides with a flag the engine already emitted —
    // engine-managed settings win over user config. Only the engine-built
    // portion of args is checked, so options the user legitimately repeats
    // (e.g. -cnf-line=...) pass through intact. Call after the engine's own
    // flags and before the tex file.
    void appendExtraOptions(std::vector<std::string>& args) const;

private:
    EngineArguments m_arguments;
};

} // namespace maketex
