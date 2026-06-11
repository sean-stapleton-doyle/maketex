#include "maketex/engines/XeLatexEngine.h"

#include <filesystem>
#include <string>
#include <vector>

namespace maketex {

// xelatex shares pdflatex's flag vocabulary, with two divergences kept in
// mind here: it has no -draftmode (its equivalent is -no-pdf, which emits
// .xdv instead of PDF), and latexmk runs it with -no-pdf plus a separate
// xdvipdfmx pass to skip PDF generation on intermediate iterations. We run
// direct-to-PDF for now; the xdv split is a possible later optimization.
std::vector<std::string> XeLatexEngine::buildArgs(const EngineInvocation& inv) const
{
    std::vector<std::string> args;
    args.emplace_back(programOr("xelatex"));
    // -recorder is mandatory: the .fls it produces is the authoritative
    // dependency source for the fixed-point loop.
    args.emplace_back("-recorder");
    // nonstopmode: no interactive prompts, but errors are written to stdout
    // (and the .log file) so DiagnosticExtractor can parse them from the
    // captured output. batchmode would suppress all terminal output entirely.
    args.emplace_back("-interaction=nonstopmode");
    if (inv.haltOnError)
        args.emplace_back("-halt-on-error");
    if (inv.shellEscape)
        args.emplace_back("-shell-escape");
    if (inv.synctex)
        args.emplace_back("-synctex=1");
    if (!inv.outputDir.empty())
        args.emplace_back("-output-directory=" + inv.outputDir.string());
    if (inv.jobName)
        args.emplace_back("-jobname=" + *inv.jobName);
    appendExtraOptions(args);
    args.emplace_back(inv.texFile.string());
    return args;
}

} // namespace maketex
