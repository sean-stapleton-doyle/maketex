#include "EngineBase.h"

#include "Launch.h"

namespace maketex {

RunResult<EngineInvocation> EngineBase::run(const EngineInvocation& inv)
{
    // Run from the tex file's directory so that relative \input{} and \include{}
    // paths resolve correctly regardless of maketex's own working directory.
    return launchProcess(buildArgs(inv), inv.texFile.parent_path(),
        invokerKind(), inv);
}

} // namespace maketex
