#include "EngineBase.h"

#include "Launch.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace maketex {

RunResult<EngineInvocation> EngineBase::run(const EngineInvocation& inv)
{
    // Run from the tex file's directory so that relative \input{} and \include{}
    // paths resolve correctly regardless of maketex's own working directory.
    return launchProcess(buildArgs(inv), inv.texFile.parent_path(),
        invokerKind(), inv);
}

void EngineBase::appendExtraOptions(std::vector<std::string>& args) const
{
    auto key = [](std::string_view arg) { return arg.substr(0, arg.find('=')); };

    // Count, not iterator/span hoisted out of the loop: push_back below may
    // reallocate. Bounding the scan to the engine-built prefix is also what
    // lets repeated user options through.
    const std::size_t engineArgCount = args.size();
    for (const auto& opt : m_arguments.extraOptions) {
        bool clashes = std::ranges::any_of(std::span(args.data(), engineArgCount),
            [&](const std::string& existing) { return key(existing) == key(opt); });
        if (!clashes)
            args.push_back(opt);
    }
}

} // namespace maketex
