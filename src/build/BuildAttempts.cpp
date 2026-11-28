#include "maketex/build/BuildAttempts.h"

#include "maketex/build/BuildHeuristics.h"

#include <format>

namespace maketex::build {

BuildResult runBuild(const BuildOptions& options, IBuildAttempts& attempts)
{
    auto choice = attempts.chooseEngine(options);
    // If no engine was configured or statically detected, reuse a Unicode
    // engine discovered by a previous retry so subsequent builds stay
    // incremental instead of failing pdflatex and resetting the database.
    if (!options.engine && choice.kind == EngineKind::PdfLaTeX && choice.reason.empty()) {
        if (auto remembered = attempts.rememberedEngine(options)) {
            choice.kind = *remembered;
            choice.reason = "remembered from previous build";
        }
    }

    auto result = attempts.attempt(options, choice.kind, /*suppressResetNotice=*/false);
    result.engineReason = choice.reason;

    // The preamble scan cannot see packages a custom class or style loads.
    // When the guess was plain pdflatex and the engine said why that was
    // wrong, retry once with the engine it asked for.
    const bool guessed = !options.engine && choice.kind == EngineKind::PdfLaTeX;
    if (!result.success && guessed) {
        if (auto needed = unicodeEngineNeeded(result.diagnostics)) {
            // Drop any partial .aux left by the failed pdflatex pass so it
            // cannot confuse the Unicode engine.
            attempts.discardPartialAux(options);

            auto retried = attempts.attempt(options, *needed, /*suppressResetNotice=*/true);
            retried.engineReason = "pdflatex failed: the document needs a Unicode engine";
            retried.retriedEngine = true;
            if (retried.success)
                attempts.rememberEngine(options, *needed);
            retried.diagnostics.insert(retried.diagnostics.begin(),
                Diagnostic {
                    .severity = Diagnostic::Severity::Info,
                    .message = std::format(
                        "the document needs {0} — rebuilt with it (set `engine = \"{0}\"` "
                        "in maketex.toml or add `% !TeX program = {0}` to skip the retry)",
                        engineKindName(*needed)),
                });
            return retried;
        }
    }
    return result;
}

} // namespace maketex::build
