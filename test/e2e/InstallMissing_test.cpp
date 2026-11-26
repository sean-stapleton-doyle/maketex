// Installing missing packages, with a fake tlmgr standing in for TeX Live's
// package manager and a fake TeX tree whose ownership the test controls.
// The engines and kpsewhich stay real; only `kpsewhich -var-value=TEXMFROOT`
// is answered by the fake.

#include "E2eSupport.h"

using namespace maketex::e2e;
namespace fs = std::filesystem;

namespace {

class InstallMissingTest : public E2eTest {
protected:
    void SetUp() override
    {
        E2eTest::SetUp();
        if (IsSkipped())
            return;
        if (!maketex::integration::hasProgram("kpsewhich"))
            GTEST_SKIP() << "not installed: kpsewhich";

        m_root = m_sandbox / "texlive";
        fs::create_directories(m_root / "tlpkg");
        fs::create_directories(m_root / "texmf-dist");
        // Where the fake tlmgr "installs" packages; on TEXINPUTS so the real
        // engine finds them.
        m_installed = m_sandbox / "installed";
        fs::create_directories(m_installed);

        m_tools = std::make_unique<FakeToolchain>(m_sandbox / "bin");
        m_tools->wrap("kpsewhich", { { "-var-value=TEXMFROOT", "echo '" + m_root.string() + "'" } });
        m_tools->add("tlmgr",
            "case \"$1\" in\n"
            "  search) echo 'tlmgr: package repository https://mirror.example (verified)'\n"
            "          echo 'maketexfakepkg:'\n"
            "          echo '\ttexmf-dist/tex/latex/maketexfakepkg/maketexfakepkg.sty' ;;\n"
            "  install) shift; for p in \"$@\"; do\n"
            "             printf '\\\\ProvidesPackage{%s}\\n' \"$p\" > '"
                + m_installed.string() + "'/\"$p\".sty; done ;;\n"
                                         "esac");
        m_cli->prependPath(m_tools->dir());
        m_cli->setEnv("TEXINPUTS", m_installed.string() + ":");

        m_doc = project("doc");
        write(m_doc / "main.tex",
            "\\documentclass{article}\n\\usepackage{maketexfakepkg}\n"
            "\\begin{document}\nHello.\n\\end{document}\n");
    }

    bool tlmgrInstalled() const
    {
        for (const auto& call : m_tools->calls())
            if (call.starts_with("tlmgr install"))
                return true;
        return false;
    }

    fs::path m_root;
    fs::path m_installed;
    fs::path m_doc;
    std::unique_ptr<FakeToolchain> m_tools;
};

} // namespace

TEST_F(InstallMissingTest, writableTreeInstallsAndRebuildsByDefault)
{
    auto r = m_cli->run({ }, m_doc);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_TRUE(fs::exists(m_doc / "main.pdf"));
    EXPECT_TRUE(tlmgrInstalled());
    EXPECT_TRUE(contains(r.err, "installing maketexfakepkg")) << r.err;
    EXPECT_TRUE(contains(r.err, "installed maketexfakepkg with tlmgr")) << r.err;
}

TEST_F(InstallMissingTest, noInstallMissingOnlySuggests)
{
    auto r = m_cli->run({ "--no-install-missing" }, m_doc);
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_FALSE(tlmgrInstalled());
    EXPECT_TRUE(contains(r.err, "sudo tlmgr install maketexfakepkg")) << r.err;
}

TEST_F(InstallMissingTest, rootOwnedTreeWithoutTerminalLeavesItToTheUser)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "root can write anywhere";
    fs::permissions(m_root / "tlpkg", fs::perms::owner_read | fs::perms::owner_exec);

    // Default: quiet, the hint says what to run.
    auto quiet = m_cli->run({ }, m_doc);
    EXPECT_EQ(quiet.exitCode, 1);
    EXPECT_FALSE(tlmgrInstalled());
    EXPECT_FALSE(contains(quiet.err, "could not install")) << quiet.err;

    // Asked for explicitly: explains why not, and how to fix it.
    auto explained = m_cli->run({ "--install-missing" }, m_doc);
    EXPECT_FALSE(tlmgrInstalled());
    EXPECT_TRUE(contains(explained.err, "chown")) << explained.err;

    fs::permissions(m_root / "tlpkg", fs::perms::owner_all);
}

TEST_F(InstallMissingTest, doctorReportsThenInstallsOnRequest)
{
    auto report = m_cli->run({ "doctor", "main.tex" }, m_doc);
    EXPECT_EQ(report.exitCode, 3);
    EXPECT_TRUE(contains(report.out, "maketexfakepkg.sty")) << report.out;
    EXPECT_FALSE(tlmgrInstalled()); // doctor diagnoses by default

    auto fixed = m_cli->run({ "doctor", "main.tex", "--install-missing" }, m_doc);
    EXPECT_EQ(fixed.exitCode, 0) << fixed.out << fixed.err;
    EXPECT_TRUE(tlmgrInstalled());
}

TEST_F(InstallMissingTest, doctorFindsLocalClassInSubdirectoryAndHandlesNoClass)
{
    auto sub = project("subdoc");
    write(sub / "paper" / "mycls.cls",
        "\\NeedsTeXFormat{LaTeX2e}\\ProvidesClass{mycls}\\LoadClass{article}\n");
    write(sub / "paper" / "main.tex",
        "\\documentclass{mycls}\n\\begin{document}Hi\\end{document}\n");

    // Run from parent directory: kpsewhich must search paper/, not cwd.
    auto ok = m_cli->run({ "doctor", "paper/main.tex" }, sub);
    EXPECT_EQ(ok.exitCode, 0) << ok.out << ok.err;
    EXPECT_FALSE(contains(ok.out, "mycls.cls  not installed")) << ok.out;

    // A fragment with neither \documentclass nor \usepackage must not underflow.
    write(sub / "fragment.tex", "Just some text.\n");
    auto frag = m_cli->run({ "doctor", "fragment.tex" }, sub);
    EXPECT_EQ(frag.exitCode, 0) << frag.out << frag.err;
    EXPECT_TRUE(contains(frag.out, "0 packages found")) << frag.out;
}

TEST_F(InstallMissingTest, batchesMultipleMissingPreamblePackagesInOneCall)
{
    // Override fake tlmgr to answer searches for both fake packages and verify
    // the search pattern is end-anchored (`/$`).
    m_tools->add("tlmgr",
        "case \"$1\" in\n"
        "  search)\n"
        "    case \"$4\" in\n"
        "      /maketexfakepkg.sty\\$) echo 'maketexfakepkg:' ;;\n"
        "      /maketexfakepkg2.sty\\$) echo 'maketexfakepkg2:' ;;\n"
        "    esac ;;\n"
        "  install) shift; for p in \"$@\"; do\n"
        "             printf '\\\\ProvidesPackage{%s}\\n' \"$p\" > '"
            + m_installed.string() + "'/\"$p\".sty; done ;;\n"
                                     "esac");

    auto doc = project("batch");
    write(doc / "main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{maketexfakepkg}\n"
        "\\usepackage{maketexfakepkg2}\n"
        "\\begin{document}\nHello.\n\\end{document}\n");

    auto r = m_cli->run({ }, doc);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_TRUE(fs::exists(doc / "main.pdf"));

    int installCalls = 0;
    std::string installCmd;
    for (const auto& call : m_tools->calls()) {
        if (call.starts_with("tlmgr install ")) {
            ++installCalls;
            installCmd = call;
        }
    }
    EXPECT_EQ(installCalls, 1) << "expected one batched tlmgr install call";
    EXPECT_TRUE(contains(installCmd, "maketexfakepkg")) << installCmd;
    EXPECT_TRUE(contains(installCmd, "maketexfakepkg2")) << installCmd;
}
