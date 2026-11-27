#include "commands/BuildCommand.h"

#include "PackageHints.h"
#include "PackageInstaller.h"
#include "render/GithubReport.h"
#include "render/InlineReport.h"
#include "render/JsonReport.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>

#include <unistd.h>

namespace maketex::cli {

namespace {

    class SystemBuildEnv final : public IBuildEnv {
    public:
        BuildResult runBuild(const BuildOptions& options) override
        {
            return maketex::runBuild(options);
        }

        BuildResult buildInstallingMissing(const BuildOptions& options,
            const InstallPolicy& policy, std::ostream& log) override
        {
            return maketex::cli::buildInstallingMissing(options, policy, log);
        }

        void addInstallHints(std::vector<Diagnostic>& diagnostics) override
        {
            maketex::cli::addInstallHints(diagnostics);
        }

        bool confirm(const std::string& question) override
        {
            return askOnTerminal(question);
        }

        bool isStdoutTty() const override { return ::isatty(STDOUT_FILENO) == 1; }

        bool isInteractiveTty() const override
        {
            return ::isatty(STDIN_FILENO) == 1 && ::isatty(STDERR_FILENO) == 1;
        }

        bool hasNoColor() const override { return std::getenv("NO_COLOR") != nullptr; }

        std::ostream& out() override { return std::cout; }
        std::ostream& err() override { return std::cerr; }
    };

} // namespace

int runBuildCommand(const EffectiveOptions& options, IBuildEnv& env)
{
    BuildResult result;
    bool environmentError = false;
    try {
        if (options.extras.installMissing) {
            // Prompting for a sudo password is only fair on a terminal and
            // with human output (machine formats own stdout).
            InstallPolicy policy {
                .interactive = options.extras.format == OutputFormat::Human
                    && env.isInteractiveTty(),
                .requested = options.extras.installMissingRequested,
                .confirm = [&env](const std::string& q) { return env.confirm(q); },
            };
            result = env.buildInstallingMissing(options.build, policy, env.err());
        } else {
            result = env.runBuild(options.build);
        }
    } catch (const std::exception& e) {
        // Database or filesystem trouble: still one report in the requested
        // format (JSON consumers rely on exactly one document on stdout).
        environmentError = true;
        result.lastError = e.what();
        result.diagnostics.push_back(Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .message = e.what(),
            .category = "environment",
        });
    }
    env.addInstallHints(result.diagnostics);
    environmentError = environmentError
        || std::ranges::any_of(result.diagnostics,
            [](const Diagnostic& d) { return d.category == "missing-tool"; });

    switch (options.extras.format) {
    case OutputFormat::Json:
        // stdout carries exactly one JSON document; anything else (config
        // warnings, reset notices) belongs on stderr.
        printJsonReport(result, options.build, env.out());
        break;
    case OutputFormat::Github:
        printGithubReport(result, env.out());
        break;
    case OutputFormat::Human: {
        bool styled = useStyledOutput(options.extras.plain,
            env.isStdoutTty(), env.hasNoColor());
        printHumanReport(result, options.build, options.extras.verbose,
            styled, env.out(), env.err());
        break;
    }
    }

    if (result.success)
        return kExitSuccess;
    return environmentError ? kExitEnvironment : kExitBuildFailed;
}

int runBuildCommand(const EffectiveOptions& options)
{
    SystemBuildEnv env;
    return runBuildCommand(options, env);
}

} // namespace maketex::cli
