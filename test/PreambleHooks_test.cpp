#include "maketex/build/PreambleHooks.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using maketex::EngineKind;
using maketex::ProcessLaunchSpec;
using maketex::ProcessResult;
using maketex::ProcessRunner;
using maketex::build::buildPreambleHooks;
using maketex::build::mintedOutputDirHook;
using maketex::build::parseMintedMajorVersion;
using maketex::build::tikzExternalHook;

namespace {

class PreambleHooksTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_preamblehooks_test"; }
};

} // namespace

TEST(PreambleHooksPureTest, parseMintedMajorVersionHandlesV2V3AndUnparsable)
{
    EXPECT_EQ(parseMintedMajorVersion(
                  "\\ProvidesPackage{minted}\n"
                  "  [2023/12/18 v2.9 Yet another Pygments shim for LaTeX]\n"),
        2);
    EXPECT_EQ(parseMintedMajorVersion(
                  "\\ProvidesPackage{minted}\n"
                  "  [2024/09/22 v3.1.2 Yet another Pygments shim for LaTeX]\n"),
        3);
    EXPECT_FALSE(parseMintedMajorVersion(
        "  [2024/09/22 vNext Yet another Pygments shim for LaTeX]\n")
            .has_value());
    EXPECT_FALSE(parseMintedMajorVersion("\\ProvidesPackage{minted}[2024/01/01]\n").has_value());

    // Beyond the 60-line scan window is ignored.
    std::string after60;
    for (int i = 0; i < 60; ++i)
        after60 += "% comment\n";
    after60 += "  [2023/12/18 v2.9 Yet another Pygments shim for LaTeX]\n";
    EXPECT_FALSE(parseMintedMajorVersion(after60).has_value());
}

TEST(PreambleHooksPureTest, mintedAndTikzHookFormatting)
{
    EXPECT_EQ(mintedOutputDirHook(".maketex"),
        "\\PassOptionsToPackage{outputdir=.maketex}{minted}");

    auto hook = tikzExternalHook("\\tikzexternalize\n", "lualatex", ".maketex");
    EXPECT_NE(hook.find("external/system call={lualatex "), std::string::npos);
    EXPECT_NE(hook.find("-output-directory=\".maketex\""), std::string::npos);

    EXPECT_FALSE(tikzExternalHook("\\usetikzlibrary{external}", "pdflatex", "out").empty());
    EXPECT_TRUE(tikzExternalHook(
        "\\tikzexternalize\n\\tikzset{external/system call={custom}}",
        "pdflatex", "out")
            .empty());
    EXPECT_TRUE(tikzExternalHook("\\usepackage{tikz}\n", "pdflatex", "out").empty());
}

TEST_F(PreambleHooksTest, buildPreambleHooksWithFakeRunner)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);

    auto tex = writeFile("main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{minted}\n"
        "\\tikzexternalize\n"
        "\\begin{document}Hi\\end{document}\n");

    // Same directory as texFile: no hooks and runner not invoked.
    int calls = 0;
    ProcessRunner countingRunner = [&](const ProcessLaunchSpec&) {
        ++calls;
        return ProcessResult { .exitCode = 0 };
    };
    EXPECT_TRUE(buildPreambleHooks(tex, m_dir, EngineKind::PdfLaTeX, "", true, countingRunner)
            .empty());
    EXPECT_EQ(calls, 0);

    // Minted v2 + TikZ externalization with shellEscape and custom program.
    fs::path styV2 = writeFile("minted_v2.sty",
        "\\ProvidesPackage{minted}[2023/12/18 v2.9 Yet another Pygments shim for LaTeX]\n");
    ProcessRunner v2Runner = [&](const ProcessLaunchSpec& spec) {
        EXPECT_EQ(spec.argv, (std::vector<std::string> { "kpsewhich", "minted.sty" }));
        return ProcessResult { .exitCode = 0, .stdoutOutput = styV2.string() + "\n" };
    };
    auto combined = buildPreambleHooks(
        tex, outDir, EngineKind::PdfLaTeX, "/opt/pdflatex", true, v2Runner);
    EXPECT_NE(combined.find("\\PassOptionsToPackage{outputdir=.maketex}{minted}"),
        std::string::npos);
    EXPECT_NE(combined.find("external/system call={/opt/pdflatex "), std::string::npos);

    // Minted v3 + shellEscape disabled -> no hooks emitted.
    fs::path styV3 = writeFile("minted_v3.sty",
        "\\ProvidesPackage{minted}[2024/09/22 v3.1 Yet another Pygments shim for LaTeX]\n");
    ProcessRunner v3Runner = [&](const ProcessLaunchSpec&) {
        return ProcessResult { .exitCode = 0, .stdoutOutput = styV3.string() + "\n" };
    };
    EXPECT_TRUE(buildPreambleHooks(tex, outDir, EngineKind::PdfLaTeX, "", false, v3Runner)
            .empty());

    // Runner throws std::system_error -> minted check degrades gracefully;
    // TikZ hook still uses engineKindName when engineProgram is empty.
    ProcessRunner throwingRunner = [](const ProcessLaunchSpec&) -> ProcessResult {
        throw std::system_error(ENOENT, std::generic_category(), "no kpsewhich");
    };
    auto tikzOnly = buildPreambleHooks(
        tex, outDir, EngineKind::XeLaTeX, "", true, throwingRunner);
    EXPECT_EQ(tikzOnly.find("PassOptionsToPackage"), std::string::npos);
    EXPECT_NE(tikzOnly.find("external/system call={xelatex "), std::string::npos);
}
