#include "maketex/DocumentProbe.h"

#include "test_utils.h"

#include <gtest/gtest.h>

using maketex::detectEngine;
using maketex::EngineKind;
using maketex::readMagicComments;
namespace fs = std::filesystem;

namespace {

class DocumentProbeTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_probe_test"; }
};

} // namespace

TEST_F(DocumentProbeTest, plainDocumentIsPdflatex)
{
    auto tex = writeFile("main.tex", "\\documentclass{article}\n\\usepackage{amsmath}\n\\begin{document}x\\end{document}\n");
    auto choice = detectEngine(tex);
    EXPECT_EQ(choice.kind, EngineKind::PdfLaTeX);
    EXPECT_TRUE(choice.reason.empty());
}

TEST_F(DocumentProbeTest, fontspecSelectsLualatex)
{
    auto tex = writeFile("main.tex", "\\documentclass{article}\n\\usepackage[no-math]{fontspec}\n");
    auto choice = detectEngine(tex);
    EXPECT_EQ(choice.kind, EngineKind::LuaLaTeX);
    EXPECT_EQ(choice.reason, "loads fontspec");
}

TEST_F(DocumentProbeTest, packageListsAreSplit)
{
    auto tex = writeFile("main.tex", "\\documentclass{article}\n\\usepackage{amsmath, unicode-math}\n");
    EXPECT_EQ(detectEngine(tex).kind, EngineKind::LuaLaTeX);
}

TEST_F(DocumentProbeTest, xetexOnlyPackageSelectsXelatex)
{
    auto tex = writeFile("main.tex", "\\documentclass{article}\n\\usepackage{fontspec}\n\\usepackage{xeCJK}\n");
    EXPECT_EQ(detectEngine(tex).kind, EngineKind::XeLaTeX);
}

TEST_F(DocumentProbeTest, directluaSelectsLualatex)
{
    auto tex = writeFile("main.tex", "\\documentclass{article}\n\\directlua{tex.print('hi')}\n");
    EXPECT_EQ(detectEngine(tex).kind, EngineKind::LuaLaTeX);
}

TEST_F(DocumentProbeTest, commentedAndBodyPackagesAreIgnored)
{
    auto tex = writeFile("main.tex",
        "\\documentclass{article}\n% \\usepackage{fontspec}\n\\begin{document}\n"
        "\\verb|\\usepackage{fontspec}|\n\\end{document}\n");
    EXPECT_EQ(detectEngine(tex).kind, EngineKind::PdfLaTeX);
}

TEST_F(DocumentProbeTest, packagesInAnInputPreambleCount)
{
    writeFile("preamble.tex", "\\usepackage{luacode}\n");
    auto tex = writeFile("main.tex", "\\documentclass{article}\n\\input{preamble}\n\\begin{document}\n");
    EXPECT_EQ(detectEngine(tex).kind, EngineKind::LuaLaTeX);
}

TEST_F(DocumentProbeTest, programMagicCommentWins)
{
    auto tex = writeFile("main.tex", "% !TeX program = xelatex\n\\documentclass{article}\n\\usepackage{luacode}\n");
    auto choice = detectEngine(tex);
    EXPECT_EQ(choice.kind, EngineKind::XeLaTeX);
    EXPECT_EQ(choice.reason, "% !TeX program");
}

TEST_F(DocumentProbeTest, magicCommentsTolerateSpellings)
{
    auto tex = writeFile("main.tex",
        "%%%%\n%\n%  !TEX TS-program = LuaLaTeX\n% !tex root=../book.tex\n");
    auto magic = readMagicComments(tex);
    EXPECT_EQ(magic.program, EngineKind::LuaLaTeX);
    ASSERT_TRUE(magic.root.has_value());
    EXPECT_EQ(magic.root->filename(), "book.tex");
}

TEST_F(DocumentProbeTest, rootCycleStopsAtTheStart)
{
    auto a = writeFile("a.tex", "% !TEX root = b.tex\n");
    writeFile("b.tex", "% !TEX root = a.tex\n");
    EXPECT_EQ(maketex::resolveRootDocument(a), a);
}
