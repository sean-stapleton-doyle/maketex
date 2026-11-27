#pragma once

#include "EffectiveOptions.h"
#include "PackageHints.h"
#include "PackageInstaller.h"

#include "maketex/DocumentProbe.h"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace maketex::cli {

// External seams for runDoctorCommand so TeX installation, engine, tool, and
// document checks are unit-testable without mutating host PATH or TeX Live.
class IDoctorEnv {
public:
    virtual ~IDoctorEnv() = default;

    virtual std::optional<std::filesystem::path> findProgram(
        const std::string& name) = 0;
    virtual std::string programVersion(const std::filesystem::path& program,
        std::string_view flag) = 0;
    virtual TexDistribution detectDistribution() = 0;
    virtual InstallCapability installCapability(TexDistribution distro,
        bool interactive) = 0;
    virtual PackageLookup packageLookup(TexDistribution distro) = 0;
    virtual DocumentRequirements analyzeRequirements(
        const std::filesystem::path& texFile) = 0;
    virtual EngineChoice chooseEngine(const BuildOptions& build) = 0;
    virtual std::set<std::string> findTexFiles(
        const std::vector<std::string>& files,
        const std::filesystem::path& workingDir,
        const std::vector<std::string>& texInputs) = 0;
    virtual bool installPackages(const std::vector<std::string>& packages,
        InstallCapability::Access access, std::ostream& log) = 0;
    virtual bool isInteractive() const = 0;
    virtual std::ostream& out() = 0;
    virtual std::ostream& err() = 0;
};

// Checks that the TeX installation has what builds need — engines, tools,
// and, given a document, the packages and programs it uses. With
// --install-missing, installs what the document lacks. Returns 0 when
// everything required is present, kExitEnvironment otherwise.
int runDoctorCommand(const EffectiveOptions& options, IDoctorEnv& env);
int runDoctorCommand(const EffectiveOptions& options);

} // namespace maketex::cli
