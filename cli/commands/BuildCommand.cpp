#include "commands/BuildCommand.h"

#include "render/GithubReport.h"
#include "render/InlineReport.h"
#include "render/JsonReport.h"
#include "render/SourceSnippet.h"

#include <format>

#include "maketex/DiagnosticFormatter.h"

#include <iostream>

namespace maketex::cli {

namespace {

    void printHumanReport(const DriverResult& result,
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
            diagnostics::format({ diagnostic }, std::cerr,
                options.extras.verbose ? diagnostics::Verbosity::Verbose
                                       : diagnostics::Verbosity::Normal);
            if (auto snippet
                = readSourceLine(diagnostic.sourcePath, diagnostic.line))
                std::cerr << std::format("  {:>4} | {}\n", diagnostic.line,
                    *snippet);
        }
        std::string passes = std::to_string(result.passes) + " pass"
            + (result.passes == 1 ? "" : "es");
        if (result.success && result.passes == 0)
            std::cout << "maketex: up to date\n";
        else if (result.success)
            std::cout << "maketex: done in " << passes << "\n";
        else
            std::cerr << "maketex: failed after " << passes << ": "
                      << result.lastError << "\n";
    }

} // namespace

int runBuildCommand(const EffectiveOptions& options)
{
    auto result = runBuild(options.build);

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

    return result.success ? 0 : 1;
}

} // namespace maketex::cli
