#pragma once

#include "EngineBase.h"

#include <string>
#include <string_view>
#include <vector>

namespace maketex {

class LuaLatexEngine : public EngineBase {
public:
    using EngineBase::EngineBase;

    std::string_view name() const override { return "lualatex"; }
    InvokerKind invokerKind() const override { return InvokerKind::LuaLaTeX; }
    std::vector<std::string> buildArgs(const EngineInvocation& inv) const override;
};

} // namespace maketex
