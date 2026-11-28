#include "maketex/DiagnosticExtractor.h"

#include "maketex/InvokerKind.h"
#include "maketex/diagnostics/LatexErrors.h"
#include "maketex/diagnostics/LatexWarnings.h"
#include "maketex/diagnostics/SourceResolver.h"
#include "maketex/diagnostics/ToolOutput.h"

#include <algorithm>
#include <optional>
#include <string>

namespace maketex::diagnostics {

namespace {

    Diagnostic makeFallback(const std::string& output)
    {
        return Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .message = "build failed — run with --verbose to see full engine output",
            .rawContext = output,
        };
    }

    // A run that never really happened (program missing) or was cut short has
    // no log worth parsing; say what happened instead.
    std::optional<Diagnostic> abnormalRun(
        const std::string& program, bool notFound, bool timedOut, bool cancelled)
    {
        if (notFound)
            return Diagnostic {
                .severity = Diagnostic::Severity::Error,
                .message = program + " is not installed (not found on PATH)",
                .category = "missing-tool",
                .missingFile = program,
            };
        if (timedOut)
            return Diagnostic {
                .severity = Diagnostic::Severity::Error,
                .message = program + " was stopped: it ran longer than the timeout",
                .category = "timeout",
            };
        if (cancelled)
            return Diagnostic {
                .severity = Diagnostic::Severity::Info,
                .message = "build cancelled",
                .category = "cancelled",
            };
        return std::nullopt;
    }

} // namespace

std::vector<Diagnostic> extract(const RunResult<EngineInvocation>& result)
{
    return extract(result, filesystemResolver());
}

std::vector<Diagnostic> extract(
    const RunResult<EngineInvocation>& result, const ISourceResolver& resolver)
{
    if (auto abnormal = abnormalRun(
            result.program, result.notFound, result.timedOut, result.cancelled))
        return { *abnormal };

    std::vector<Diagnostic> diags;
    switch (result.kind) {
    case InvokerKind::PdfLaTeX:
    case InvokerKind::XeLaTeX:
    case InvokerKind::LuaLaTeX:
        if (result.success()) {
            diags = extractLatexWarnings(
                result.stdoutOutput, result.invocation.texFile, resolver);
        } else {
            // Errors first; the warnings of a failed pass still explain a lot
            // (an undefined reference before the fatal error), but box-layout
            // noise does not.
            diags = extractLatexErrors(
                result.stdoutOutput, result.invocation.texFile, resolver);
            for (auto& w : extractLatexWarnings(
                     result.stdoutOutput, result.invocation.texFile, resolver))
                if (!w.category.starts_with("overfull-")
                    && !w.category.starts_with("underfull-"))
                    diags.push_back(std::move(w));
        }
        break;
    default:
        break;
    }

    if (!result.success() && diags.empty())
        diags.push_back(makeFallback(result.stdoutOutput));

    return diags;
}

std::vector<Diagnostic> extract(const RunResult<ToolInvocation>& result)
{
    if (auto abnormal = abnormalRun(
            result.program, result.notFound, result.timedOut, result.cancelled))
        return { *abnormal };

    // Tools split their reports between the streams (makeindex writes to
    // stderr); parse both.
    std::string output = result.stdoutOutput;
    if (!result.stderrOutput.empty()) {
        if (!output.empty() && output.back() != '\n')
            output += '\n';
        output += result.stderrOutput;
    }

    auto diags = extractToolOutput(result.kind, output);

    // A successful run still reports its warnings (a citation with no database
    // entry), but nothing that claims it failed.
    if (result.success()) {
        std::erase_if(diags, [](const Diagnostic& d) {
            return d.severity == Diagnostic::Severity::Error;
        });
        return diags;
    }
    if (std::ranges::none_of(diags, [](const Diagnostic& d) {
            return d.severity == Diagnostic::Severity::Error;
        }))
        diags.insert(diags.begin(), makeFallback(output));
    return diags;
}

} // namespace maketex::diagnostics
