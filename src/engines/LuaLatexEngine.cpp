#include "maketex/engines/LuaLatexEngine.h"

#include <filesystem>
#include <string>
#include <vector>

namespace maketex {

// lualatex accepts the same single-dash flag spellings as pdflatex (its
// --help advertises double-dash, but web2c parses both). Caveat carried by
// the parsers, not here: lualatex's .fls does not reliably record Lua file
// inputs, and luatex-cache files must not count as changed dependencies.
std::vector<std::string> LuaLatexEngine::buildArgs(const EngineInvocation& inv) const
{
    std::vector<std::string> args;
    args.emplace_back(programOr("lualatex"));
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
