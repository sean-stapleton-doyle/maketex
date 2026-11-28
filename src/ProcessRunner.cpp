#include "maketex/ProcessRunner.h"

namespace maketex {

ProcessResult runSystemProcess(const ProcessLaunchSpec& spec)
{
    return Process(spec.argv, spec.workingDir, spec.extraEnv).wait(spec.limits);
}

} // namespace maketex
