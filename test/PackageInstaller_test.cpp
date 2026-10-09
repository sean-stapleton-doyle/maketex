#include "PackageInstaller.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using maketex::Diagnostic;
using maketex::cli::InstallCapability;
using maketex::cli::installCapability;
using maketex::cli::PackageLookup;
using maketex::cli::planInstall;
using maketex::cli::TexDistribution;
namespace fs = std::filesystem;

namespace {

Diagnostic missing(std::string file, std::string category = "missing-file")
{
    return Diagnostic { .category = std::move(category), .missingFile = std::move(file) };
}

const PackageLookup kNoLookup = [](const std::string&) { return std::optional<std::string> { }; };

// A fake TeX Live: `kpsewhich -var-value=TEXMFROOT` answers with a temp
// directory whose permissions the test controls.
class FakeTexTreeTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_installer_test"; }

    void SetUp() override
    {
        TempDirFixture::SetUp();
        m_root = m_dir / "texlive";
        fs::create_directories(m_root / "tlpkg");
        fs::create_directories(m_root / "texmf-dist");
        fs::create_directories(m_dir / "bin");
        auto kpsewhich = m_dir / "bin" / "kpsewhich";
        std::ofstream(kpsewhich) << "#!/bin/sh\necho " << m_root.string() << "\n";
        fs::permissions(kpsewhich, fs::perms::owner_all);
        m_savedPath = std::getenv("PATH");
        ::setenv("PATH", ((m_dir / "bin").string() + ":" + m_savedPath).c_str(), 1);
    }

    void TearDown() override
    {
        ::setenv("PATH", m_savedPath.c_str(), 1);
        fs::permissions(m_root / "tlpkg", fs::perms::owner_all);
        TempDirFixture::TearDown();
    }

    void makeTreeReadOnly()
    {
        fs::permissions(m_root / "tlpkg", fs::perms::owner_read | fs::perms::owner_exec);
    }

    fs::path m_root;
    std::string m_savedPath;
};

} // namespace

TEST(PlanInstall, resolvesPackagesAndDeduplicates)
{
    auto plan = planInstall({ missing("minted.sty"), missing("minted.sty"),
                                missing("biber", "missing-tool"), missing("makeglossaries", "missing-tool"),
                                missing("glossaries.sty") },
        kNoLookup);
    EXPECT_EQ(plan.packages, (std::vector<std::string> { "minted", "biber", "glossaries" }));
    EXPECT_TRUE(plan.unresolved.empty());
}

TEST(PlanInstall, nonTexAndUserFilesAreNotInstalled)
{
    auto plan = planInstall({ missing("pygmentize", "missing-tool"), missing("chapter.tex"),
                                missing("obscure.sty") },
        kNoLookup);
    EXPECT_TRUE(plan.packages.empty());
    // chapter.tex is the user's own file: not even "unresolved".
    EXPECT_EQ(plan.unresolved, (std::vector<std::string> { "pygmentize", "obscure.sty" }));
}

TEST(InstallCapability, onlyTexLiveCanInstall)
{
    EXPECT_EQ(installCapability(TexDistribution::Debian, true).access,
        InstallCapability::Access::Unavailable);
    EXPECT_EQ(installCapability(TexDistribution::MiKTeX, true).access,
        InstallCapability::Access::Unavailable);
}

TEST_F(FakeTexTreeTest, writableTreeInstallsDirectly)
{
    EXPECT_EQ(installCapability(TexDistribution::TexLive, false).access,
        InstallCapability::Access::Direct);
}

TEST_F(FakeTexTreeTest, rootOwnedTreeNeedsATerminalForSudo)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "root can write anywhere";
    makeTreeReadOnly();
    EXPECT_EQ(installCapability(TexDistribution::TexLive, true).access,
        InstallCapability::Access::Sudo);
    auto noTerminal = installCapability(TexDistribution::TexLive, false);
    EXPECT_EQ(noTerminal.access, InstallCapability::Access::Unavailable);
    EXPECT_NE(noTerminal.reason.find("chown"), std::string::npos) << noTerminal.reason;
}
