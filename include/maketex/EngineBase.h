#pragma once

#include "maketex/EngineArguments.h"
#include "maketex/EngineInvocation.h"
#include "maketex/IEngine.h"
#include "maketex/RunResult.h"

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

    RunResult<EngineInvocation> run(const EngineInvocation& inv,
        const WaitLimits& limits) override;

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

    // Assembles the flag vocabulary shared by the web2c TeX engines
    // (pdflatex/xelatex/lualatex): -recorder, -interaction=nonstopmode,
    // -file-line-error, the optional halt/shell-escape/synctex/output-directory
    // /jobname flags, the user's extra options, and the tex file. Engines that
    // share this vocabulary call this from buildArgs() with their program name.
    std::vector<std::string> texEngineArgs(std::string_view program,
        const EngineInvocation& inv) const;

private:
    EngineArguments m_arguments;
};

} // namespace maketex
