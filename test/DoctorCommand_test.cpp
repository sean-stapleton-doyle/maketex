#include "commands/DoctorCommand.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using maketex::BuildOptions;
using maketex::DocumentRequirements;
using maketex::EngineChoice;
using maketex::EngineKind;
using maketex::cli::EffectiveOptions;
using maketex::cli::IDoctorEnv;
using maketex::cli::InstallCapability;
using maketex::cli::kExitEnvironment;
using maketex::cli::kExitSuccess;
using maketex::cli::PackageLookup;
using maketex::cli::runDoctorCommand;
using maketex::cli::TexDistribution;

namespace {

class FakeDoctorEnv : public IDoctorEnv {
public:
    FakeDoctorEnv()
    {
        for (const char* prog : { "kpsewhich", "pdflatex", "xelatex", "lualatex",
                 "bibtex", "biber", "makeindex", "makeglossaries",
                 "bib2gls", "pythontex", "pygmentize", "inkscape" }) {
            programs[prog] = fs::path("/usr/bin") / prog;
            versions[prog] = std::string(prog) + " 1.0";
        }
        versions["makeindex"] = "";
    }

    std::map<std::string, fs::path> programs;
    std::map<std::string, std::string> versions;
    TexDistribution distro = TexDistribution::TexLive;
    InstallCapability capability { .access = InstallCapability::Access::Direct };
    std::map<std::string, std::string> packageMap;
    DocumentRequirements requirements;
    EngineChoice engineChoice { .kind = EngineKind::PdfLaTeX };
    std::set<std::string> texFiles;
    bool interactive = false;
    bool installSucceeds = true;
    // When true, installPackages adds installed `<pkg>.sty` to texFiles.
    bool installPopulatesTexFiles = false;

    std::vector<std::vector<std::string>> installedBatches;
    std::ostringstream outStream;
    std::ostringstream errStream;

    std::optional<fs::path> findProgram(const std::string& name) override
    {
        auto it = programs.find(name);
        return it == programs.end() ? std::nullopt : std::optional { it->second };
    }

    std::string programVersion(const fs::path& program, std::string_view flag) override
    {
        if (flag.empty())
            return { };
        auto it = versions.find(program.filename().string());
        return it == versions.end() ? std::string { } : it->second;
    }

    TexDistribution detectDistribution() override { return distro; }

    InstallCapability installCapability(TexDistribution, bool) override
    {
        return capability;
    }

    PackageLookup packageLookup(TexDistribution) override
    {
        return [this](const std::string& file) -> std::optional<std::string> {
            auto it = packageMap.find(file);
            return it == packageMap.end() ? std::nullopt : std::optional { it->second };
        };
    }

    DocumentRequirements analyzeRequirements(const fs::path&) override
    {
        return requirements;
    }

    EngineChoice chooseEngine(const BuildOptions&) override { return engineChoice; }

    std::set<std::string> findTexFiles(const std::vector<std::string>& files,
        const fs::path&, const std::vector<std::string>&) override
    {
        std::set<std::string> found;
        for (const auto& f : files)
            if (texFiles.contains(f))
                found.insert(f);
        return found;
    }

    bool installPackages(const std::vector<std::string>& packages,
        InstallCapability::Access, std::ostream& log) override
    {
        installedBatches.push_back(packages);
        log << "installing packages\n";
        if (installPopulatesTexFiles)
            for (const auto& p : packages)
                texFiles.insert(p + ".sty");
        return installSucceeds;
    }

    bool isInteractive() const override { return interactive; }
    std::ostream& out() override { return outStream; }
    std::ostream& err() override { return errStream; }
};

} // namespace

TEST(DoctorCommandTest, healthyEnvironmentWithNoDocumentSucceedsAndShowsTip)
{
    FakeDoctorEnv env;
    EffectiveOptions opts;

    int code = runDoctorCommand(opts, env);

    EXPECT_EQ(code, kExitSuccess);
    EXPECT_NE(env.outStream.str().find("TeX Live  kpsewhich 1.0"), std::string::npos);
    EXPECT_NE(env.outStream.str().find("makeindex  for indexes and nomenclature"),
        std::string::npos);
    EXPECT_NE(env.outStream.str().find("pass a document"), std::string::npos);
    EXPECT_NE(env.outStream.str().find("maketex doctor: everything needed is installed"),
        std::string::npos);
}

TEST(DoctorCommandTest, missingTexInstallationAndNoEnginesReportsProblems)
{
    FakeDoctorEnv env;
    env.programs.clear();
    EffectiveOptions opts;

    int code = runDoctorCommand(opts, env);

    EXPECT_EQ(code, kExitEnvironment);
    EXPECT_NE(env.outStream.str().find("no TeX installation on PATH"), std::string::npos);
    EXPECT_NE(env.outStream.str().find("no TeX engine installed"), std::string::npos);
    EXPECT_NE(env.outStream.str().find("2 problems found"), std::string::npos);
}

TEST(DoctorCommandTest, rendersAllDistributionsAndInstallCapabilities)
{
    struct Case {
        TexDistribution distro;
        std::string expectedName;
        InstallCapability cap;
        std::string expectedCapText;
    };
    std::vector<Case> cases {
        { TexDistribution::Debian, "TeX Live (Debian package)",
            { .access = InstallCapability::Access::Unavailable, .reason = "use apt" }, "use apt" },
        { TexDistribution::Fedora, "TeX Live (Fedora package)",
            { .access = InstallCapability::Access::Sudo, .reason = "root-owned tree" }, "root-owned tree" },
        { TexDistribution::MiKTeX, "MiKTeX",
            { .access = InstallCapability::Access::Unavailable, .reason = "use miktex" }, "use miktex" },
        { TexDistribution::Unknown, "unknown distribution",
            { .access = InstallCapability::Access::Direct }, "tlmgr can install" },
    };

    for (const auto& c : cases) {
        FakeDoctorEnv env;
        env.distro = c.distro;
        env.capability = c.cap;
        EffectiveOptions opts;

        EXPECT_EQ(runDoctorCommand(opts, env), kExitSuccess);
        EXPECT_NE(env.outStream.str().find(c.expectedName), std::string::npos);
        EXPECT_NE(env.outStream.str().find(c.expectedCapText), std::string::npos);
    }
}

TEST(DoctorCommandTest, flagsMissingRequiredEngineToolAndPackagesForDocument)
{
    FakeDoctorEnv env;
    env.programs.erase("xelatex");
    env.programs.erase("biber");
    env.programs.erase("pygmentize"); // optional for this doc
    env.engineChoice = EngineChoice { .kind = EngineKind::XeLaTeX, .reason = "loads fontspec" };
    env.requirements = DocumentRequirements {
        .documentClass = "article",
        .packages = { "fontspec", "missingpkg" },
        .programs = { { "biber", "uses biblatex" } },
    };
    env.texFiles = { "article.cls", "fontspec.sty" };
    env.packageMap["missingpkg.sty"] = "missingpkg";

    EffectiveOptions opts;
    opts.build.texFile = "/project/thesis.tex";

    int code = runDoctorCommand(opts, env);

    EXPECT_EQ(code, kExitEnvironment);
    const auto output = env.outStream.str();
    EXPECT_NE(output.find("xelatex  not installed — this document needs it"), std::string::npos);
    EXPECT_NE(output.find("biber  not installed — this document uses biblatex"), std::string::npos);
    EXPECT_NE(output.find("pygmentize  not installed (needed for minted code listings)"),
        std::string::npos);
    EXPECT_NE(output.find("missingpkg.sty  not installed"), std::string::npos);
    EXPECT_NE(output.find("xelatex (loads fontspec)"), std::string::npos);
    EXPECT_NE(output.find("3 problems found"), std::string::npos);
}

TEST(DoctorCommandTest, warnsAboutShellEscapeAndHandlesSingularPluralPackages)
{
    // Class + 1 package + shell escape warning (still exit 0).
    {
        FakeDoctorEnv env;
        env.requirements = DocumentRequirements {
            .documentClass = "article",
            .packages = { "minted" },
            .needsShellEscape = true,
        };
        env.texFiles = { "article.cls", "minted.sty" };

        EffectiveOptions opts;
        opts.build.texFile = "/project/code.tex";
        opts.build.engine = EngineKind::PdfLaTeX;

        EXPECT_EQ(runDoctorCommand(opts, env), kExitSuccess);
        EXPECT_NE(env.outStream.str().find("class and 1 package found"), std::string::npos);
        EXPECT_NE(env.outStream.str().find("(set by flag or config)"), std::string::npos);
        EXPECT_NE(env.outStream.str().find("shell escape"), std::string::npos);
    }

    // No class + 2 packages + shell escape enabled (no warning).
    {
        FakeDoctorEnv env;
        env.requirements = DocumentRequirements {
            .packages = { "amsmath", "graphicx" },
            .needsShellEscape = true,
        };
        env.texFiles = { "amsmath.sty", "graphicx.sty" };

        EffectiveOptions opts;
        opts.build.texFile = "/project/frag.tex";
        opts.build.shellEscape = true;

        EXPECT_EQ(runDoctorCommand(opts, env), kExitSuccess);
        EXPECT_NE(env.outStream.str().find("2 packages found"), std::string::npos);
        EXPECT_NE(env.outStream.str().find("(default)"), std::string::npos);
        EXPECT_EQ(env.outStream.str().find("shell escape"), std::string::npos);
    }
}

TEST(DoctorCommandTest, installMissingInstallsWhenAvailableAndSkipsWhenUnavailable)
{
    EffectiveOptions opts;
    opts.build.texFile = "/project/paper.tex";
    opts.extras.installMissingRequested = true;

    // Direct access: installs missing package before final check → exit 0.
    {
        FakeDoctorEnv env;
        env.requirements = DocumentRequirements { .packages = { "fancyhdr" } };
        env.packageMap["fancyhdr.sty"] = "fancyhdr";
        env.installPopulatesTexFiles = true;

        EXPECT_EQ(runDoctorCommand(opts, env), kExitSuccess);
        ASSERT_EQ(env.installedBatches.size(), 1u);
        EXPECT_EQ(env.installedBatches[0], std::vector<std::string> { "fancyhdr" });
        EXPECT_NE(env.errStream.str().find("installing packages"), std::string::npos);
    }

    // Unavailable access: never invokes installPackages → reports 1 problem.
    {
        FakeDoctorEnv env;
        env.capability = { .access = InstallCapability::Access::Unavailable, .reason = "no tlmgr" };
        env.requirements = DocumentRequirements { .packages = { "fancyhdr" } };
        env.packageMap["fancyhdr.sty"] = "fancyhdr";

        EXPECT_EQ(runDoctorCommand(opts, env), kExitEnvironment);
        EXPECT_TRUE(env.installedBatches.empty());
        EXPECT_NE(env.outStream.str().find("1 problem found"), std::string::npos);
    }
}
