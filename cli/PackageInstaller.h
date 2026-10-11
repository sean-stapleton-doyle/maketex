#pragma once

#include "PackageHints.h"

#include "maketex/Build.h"

#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace maketex::cli {

// Whether maketex can run `tlmgr install`, and how.
struct InstallCapability {
    enum class Access {
        Direct, // the TeX tree is writable by this user
        Sudo, // root-owned tree, but a terminal is attached to ask for a password
        Unavailable,
    };
    Access access = Access::Unavailable;
    // Why Unavailable, or a note for Sudo; empty for Direct.
    std::string reason;
};

// interactive: maketex may prompt on this terminal (a plain `maketex build`
// on a tty — never watch, CI, or machine-readable output).
InstallCapability installCapability(TexDistribution distro, bool interactive);

// The TeX Live packages that would fix diagnostics' missing packages and
// tools; files no package provides (pygmentize, the user's own \input
// files) go to `unresolved`.
struct InstallPlan {
    std::vector<std::string> packages;
    std::vector<std::string> unresolved;
};
InstallPlan planInstall(const std::vector<Diagnostic>& diagnostics, const PackageLookup& lookup);

// Runs `tlmgr install <packages>` (under sudo for Access::Sudo, on the
// terminal so it can prompt). Progress and failures go to `log`. Returns
// whether tlmgr succeeded.
bool installPackages(const std::vector<std::string>& packages,
    InstallCapability::Access access, std::ostream& log);

// When and how buildInstallingMissing may install.
struct InstallPolicy {
    // maketex may prompt on this terminal (a sudo password, a question).
    bool interactive = false;
    // The user asked for installs (--install-missing or the config key)
    // rather than getting the default. Default installs ask before using
    // sudo, and stay silent when they can't run — the install hint on the
    // error already says what to do.
    bool requested = false;
    // Asks a yes/no question; called only for an unrequested sudo install
    // on an interactive terminal. Unset answers no.
    std::function<bool(const std::string& question)> confirm;
};

// Asks `question [Y/n]` on stderr and reads the answer from stdin; an
// empty answer is yes.
bool askOnTerminal(const std::string& question);

// runBuild, and while it fails for want of packages tlmgr can install:
// install them and build again. TeX stops at the first missing package, so
// this can take several rounds; a package is never tried twice. The result
// carries an Info diagnostic per installation (and, when requested, why
// none was possible).
BuildResult buildInstallingMissing(const BuildOptions& options, const InstallPolicy& policy,
    std::ostream& log);

} // namespace maketex::cli
