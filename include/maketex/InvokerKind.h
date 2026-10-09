#pragma once

namespace maketex {

enum class InvokerKind { PdfLaTeX,
    XeLaTeX,
    LuaLaTeX,
    Bibtex,
    Biber,
    Makeindex,
    Makeglossaries,
    Pythontex };

} // namespace maketex
