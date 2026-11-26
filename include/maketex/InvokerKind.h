#pragma once

namespace maketex {

enum class InvokerKind { PdfLaTeX,
    XeLaTeX,
    LuaLaTeX,
    Bibtex,
    Biber,
    Makeindex,
    Makeglossaries,
    Bib2gls,
    Pythontex,
    Custom };

} // namespace maketex
