#include "commands/BuildCommand.h"

#include "PackageHints.h"
#include "PackageInstaller.h"
#include "PathUtil.h"
#include "render/GithubReport.h"
#include "render/InlineReport.h"
#include "render/JsonReport.h"
#include "render/SourceSnippet.h"

#include <format>

#include "maketex/DiagnosticFormatter.h"

#include <algorithm>
#include <exception>
#include <iostream>

#include <unistd.h>

namespace maketex::cli {

namespace {

    void printHumanReport(const BuildResult& result,
        const EffectiveOptions& options)
    {
        if (useStyledOutput(options.extras.plain)) {
            printInline(buildReport(result, options.build, options.extras.verbose));
            return;
        }

        // One diagnostic at a time so the source snippet (CLI-layer file
        // I/O the library formatter deliberately avoids) lands under its
        // diagnostic, not after the whole list.
        for (const auto& diagnostic : result.diagnostics) {
            // The hint goes last, under the quoted source line.
            auto withoutHint = diagnostic;
            withoutHint.hint.clear();
            diagnostics::format({ withoutHint }, std::cerr,
                options.extras.verbose ? diagnostics::Verbosity::Verbose
                                       : diagnostics::Verbosity::Normal);
            if (auto snippet
                = readSourceLine(diagnostic.sourcePath, diagnostic.line))
                std::cerr << std::format("  {:>4} | {}\n", diagnostic.line,
                    *snippet);
            if (!diagnostic.hint.empty())
                std::cerr << "  hint: " << diagnostic.hint << "\n";
        }
        // Why the build took the passes it did — "pass 2: main.aux changed".
        if (options.extras.verbose)
            for (const auto& reason : result.rerunReasons)
                std::cerr << "maketex: " << reason << "\n";
        if (!result.engineReason.empty())
            std::cerr << "maketex: engine " << engineLabel(result.engine, result.engineReason) << "\n";
        std::string passes = passCount(result.passes);
        if (result.upToDate)
            std::cout << "maketex: up to date\n";
        else if (result.success && result.passes == 0)
            std::cout << "maketex: done (tools only, no engine pass needed)\n";
        else if (result.success)
            std::cout << "maketex: done in " << passes << "\n";
        else
            std::cerr << "maketex: failed after " << passes << ": "
                      << result.lastError << "\n";
    }

} // namespace

int runBuildCommand(const EffectiveOptions& options)
{
    BuildResult result;
    bool environmentError = false;
    try {
        if (options.extras.installMissing) {
            // Prompting for a sudo password is only fair on a terminal and
            // with human output (machine formats own stdout).
            InstallPolicy policy {
                .interactive = options.extras.format == OutputFormat::Human
                    && ::isatty(STDIN_FILENO) && ::isatty(STDERR_FILENO),
                .requested = options.extras.installMissingRequested,
                .confirm = askOnTerminal,
            };
            result = buildInstallingMissing(options.build, policy, std::cerr);
        } else {
            result = runBuild(options.build);
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
    addInstallHints(result.diagnostics);
    environmentError = environmentError
        || std::ranges::any_of(result.diagnostics,
            [](const Diagnostic& d) { return d.category == "missing-tool"; });

    switch (options.extras.format) {
    case OutputFormat::Json:
        // stdout carries exactly one JSON document; anything else (config
        // warnings, reset notices) belongs on stderr.
        printJsonReport(result, options.build, std::cout);
        break;
    case OutputFormat::Github:
        printGithubReport(result, std::cout);
        break;
    case OutputFormat::Human:
        printHumanReport(result, options);
        break;
    }

    if (result.success)
        return kExitSuccess;
    return environmentError ? kExitEnvironment : kExitBuildFailed;
}

} // namespace maketex::cli
