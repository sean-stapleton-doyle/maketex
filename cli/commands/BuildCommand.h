#pragma once

#include "EffectiveOptions.h"
#include "PackageInstaller.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace maketex::cli {

// External seams for runBuildCommand so build orchestration, error mapping,
// output format dispatch, and install-policy wiring are unit-testable without
// invoking TeX or mutating process stdio.
class IBuildEnv {
public:
    virtual ~IBuildEnv() = default;

    virtual BuildResult runBuild(const BuildOptions& options) = 0;
    virtual BuildResult buildInstallingMissing(const BuildOptions& options,
        const InstallPolicy& policy, std::ostream& log) = 0;
    virtual void addInstallHints(std::vector<Diagnostic>& diagnostics) = 0;
    virtual bool confirm(const std::string& question) = 0;
    virtual bool isStdoutTty() const = 0;
    virtual bool isInteractiveTty() const = 0;
    virtual bool hasNoColor() const = 0;
    virtual std::ostream& out() = 0;
    virtual std::ostream& err() = 0;
};

// One-shot build. Returns the process exit code.
int runBuildCommand(const EffectiveOptions& options, IBuildEnv& env);
int runBuildCommand(const EffectiveOptions& options);

} // namespace maketex::cli
