#pragma once

#include "EngineBase.h"

#include <string>
#include <string_view>
#include <vector>

namespace maketex {

class XeLatexEngine : public EngineBase {
public:
    using EngineBase::EngineBase;

    std::string_view name() const override { return "xelatex"; }
    InvokerKind invokerKind() const override { return InvokerKind::XeLaTeX; }
    std::vector<std::string> buildArgs(const EngineInvocation& inv) const override;
};

} // namespace maketex
