#pragma once

#include "maketex/EngineBase.h"

#include <string>
#include <string_view>
#include <vector>

namespace maketex {

class PdfLatexEngine : public EngineBase {
public:
    using EngineBase::EngineBase;

    std::string_view name() const override { return "pdflatex"; }
    InvokerKind invokerKind() const override { return InvokerKind::PdfLaTeX; }
    std::vector<std::string> buildArgs(const EngineInvocation& inv) const override;
};

} // namespace maketex
