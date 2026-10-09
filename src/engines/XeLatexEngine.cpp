#include "maketex/engines/XeLatexEngine.h"

namespace maketex {

// xelatex shares pdflatex's flag vocabulary, with two divergences kept in
// mind for a future optimisation: it has no -draftmode (its equivalent is
// -no-pdf, which emits .xdv instead of PDF), and latexmk runs it with
// -no-pdf plus a separate xdvipdfmx pass to skip PDF generation on
// intermediate iterations. We run direct-to-PDF for now; the xdv split is a
// possible later change. Flag assembly is shared via EngineBase::texEngineArgs.
std::vector<std::string> XeLatexEngine::buildArgs(const EngineInvocation& inv) const
{
    return texEngineArgs("xelatex", inv);
}

} // namespace maketex
