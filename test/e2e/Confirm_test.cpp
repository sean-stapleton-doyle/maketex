// The question maketex asks before installing with sudo, answered on a real
// pseudo-terminal. sudo and tlmgr are fakes; the TeX tree is a read-only
// stand-in, so maketex chooses the sudo route.

#include "E2eSupport.h"

using namespace maketex::e2e;
namespace fs = std::filesystem;

namespace {

class ConfirmTest : public E2eTest {
protected:
    void SetUp() override
    {
        E2eTest::SetUp();
        if (IsSkipped())
            return;
        if (::geteuid() == 0)
            GTEST_SKIP() << "root can write anywhere";
        m_root = m_sandbox / "texlive";
        fs::create_directories(m_root / "tlpkg");
        fs::create_directories(m_root / "texmf-dist");
        fs::permissions(m_root / "tlpkg", fs::perms::owner_read | fs::perms::owner_exec);
        m_installed = m_sandbox / "installed";
        fs::create_directories(m_installed);

        m_tools = std::make_unique<FakeToolchain>(m_sandbox / "bin");
        m_tools->wrap("kpsewhich", { { "-var-value=TEXMFROOT", "echo '" + m_root.string() + "'" } });
        m_tools->add("sudo", "exec \"$@\"");
        m_tools->add("tlmgr",
            "case \"$1\" in\n"
            "  search) echo 'maketexfakepkg:' ;;\n"
            "  install) printf '\\\\ProvidesPackage{maketexfakepkg}\\n' > '"
                + m_installed.string() + "/maketexfakepkg.sty' ;;\n"
                                         "esac");
        m_cli->prependPath(m_tools->dir());
        m_cli->setEnv("TEXINPUTS", m_installed.string() + ":");

        m_doc = project("doc");
        write(m_doc / "main.tex",
            "\\documentclass{article}\n\\usepackage{maketexfakepkg}\n"
            "\\begin{document}\nHello.\n\\end{document}\n");
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::permissions(m_root / "tlpkg", fs::perms::owner_all, ec);
        E2eTest::TearDown();
    }

    bool ranSudo() const
    {
        for (const auto& call : m_tools->calls())
            if (call.starts_with("sudo tlmgr install"))
                return true;
        return false;
    }

    fs::path m_root;
    fs::path m_installed;
    fs::path m_doc;
    std::unique_ptr<FakeToolchain> m_tools;
};

} // namespace

TEST_F(ConfirmTest, yesInstallsWithSudoAndBuilds)
{
    auto r = m_cli->runInTerminal({ "--plain" }, m_doc, "y\n");
    EXPECT_TRUE(contains(r.out, "Install with `sudo tlmgr install maketexfakepkg`? [Y/n]")) << r.out;
    EXPECT_TRUE(ranSudo());
    EXPECT_EQ(r.exitCode, 0) << r.out;
    EXPECT_TRUE(fs::exists(m_doc / "main.pdf"));
}

TEST_F(ConfirmTest, noLeavesTheSystemAlone)
{
    auto r = m_cli->runInTerminal({ "--plain" }, m_doc, "n\n");
    EXPECT_TRUE(contains(r.out, "[Y/n]")) << r.out;
    EXPECT_FALSE(ranSudo());
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_TRUE(contains(r.out, "hint: install it: sudo tlmgr install maketexfakepkg")) << r.out;
}

TEST_F(ConfirmTest, explicitRequestSkipsTheQuestion)
{
    auto r = m_cli->runInTerminal({ "--plain", "--install-missing" }, m_doc, "");
    EXPECT_FALSE(contains(r.out, "[Y/n]")) << r.out;
    EXPECT_TRUE(ranSudo());
    EXPECT_EQ(r.exitCode, 0) << r.out;
}
