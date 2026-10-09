#include "maketex/engines/PdfLatexEngine.h"

namespace maketex {

// Shares EngineBase::texEngineArgs — the flag vocabulary is identical across
// the web2c TeX trio; only the program name differs.
std::vector<std::string> PdfLatexEngine::buildArgs(const EngineInvocation& inv) const
{
    return texEngineArgs("pdflatex", inv);
}

} // namespace maketex
