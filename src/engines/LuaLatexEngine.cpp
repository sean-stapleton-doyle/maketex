#include "maketex/engines/LuaLatexEngine.h"

namespace maketex {

// lualatex accepts the same single-dash flag spellings as pdflatex (its
// --help advertises double-dash, but web2c parses both). Caveat carried by
// the parsers, not here: lualatex's .fls does not reliably record Lua file
// inputs, and luatex-cache files must not count as changed dependencies.
// Flag assembly is shared via EngineBase::texEngineArgs.
std::vector<std::string> LuaLatexEngine::buildArgs(const EngineInvocation& inv) const
{
    return texEngineArgs("lualatex", inv);
}

} // namespace maketex
