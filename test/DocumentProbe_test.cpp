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

TEST_F(DocumentProbeTest, requirementsListToolsAndShellEscape)
{
    writeFile("chapter.tex", "\\printindex\n\\bibliography{refs}\n");
    auto tex = writeFile("main.tex",
        "\\documentclass{book}\n"
        "\\usepackage{biblatex, minted}\n\\usepackage{makeidx}\n\\makeindex\n"
        "\\begin{document}\n\\include{chapter}\n\\end{document}\n");
    auto req = maketex::analyzeRequirements(tex);
    EXPECT_EQ(req.documentClass, "book");
    EXPECT_EQ(req.packages, (std::vector<std::string> { "biblatex", "makeidx", "minted" }));
    std::vector<std::string> programs;
    for (const auto& [program, why] : req.programs)
        programs.push_back(program);
    // \bibliography lives in the \include'd chapter.
    EXPECT_EQ(programs, (std::vector<std::string> { "biber", "bibtex", "makeindex", "pygmentize" }));
    EXPECT_TRUE(req.needsShellEscape);
}

TEST_F(DocumentProbeTest, biblatexWithBibtexBackendNeedsBibtex)
{
    auto tex = writeFile("main.tex", "\\documentclass{article}\n\\usepackage[backend=bibtex]{biblatex}\n");
    auto req = maketex::analyzeRequirements(tex);
    ASSERT_EQ(req.programs.size(), 1u);
    EXPECT_EQ(req.programs[0].first, "bibtex");
}

TEST_F(DocumentProbeTest, subfilesChaptersAreNotRootDocuments)
{
    writeFile("thesis.tex", "\\documentclass{book}\n\\usepackage{subfiles}\n\\begin{document}\\end{document}\n");
    writeFile("ch1.tex", "\\documentclass[thesis.tex]{subfiles}\n\\begin{document}Ch 1\\end{document}\n");
    auto roots = maketex::findRootDocuments(m_dir);
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].filename(), "thesis.tex");
}

TEST_F(DocumentProbeTest, tikzGraphdrawingSelectsLualatex)
{
    auto tex = writeFile("main.tex",
        "\\documentclass{article}\n\\usepackage{tikz}\n"
        "\\usetikzlibrary{arrows, graphdrawing}\n\\begin{document}\\end{document}\n");
    auto choice = detectEngine(tex);
    EXPECT_EQ(choice.kind, EngineKind::LuaLaTeX);
    EXPECT_NE(choice.reason.find("graphdrawing"), std::string::npos);
}

TEST_F(DocumentProbeTest, detectsTikzExternalizeSvgNomenclAndBib2gls)
{
    auto tikz = writeFile("tikz.tex",
        "\\documentclass{article}\n\\usepackage{tikz}\n"
        "\\usetikzlibrary{external}\n\\tikzexternalize\n\\begin{document}\\end{document}\n");
    EXPECT_TRUE(maketex::analyzeRequirements(tikz).needsShellEscape);

    auto svg = writeFile("svg.tex",
        "\\documentclass{article}\n\\usepackage{svg}\n\\begin{document}\\end{document}\n");
    auto svgReq = maketex::analyzeRequirements(svg);
    EXPECT_TRUE(svgReq.needsShellEscape);
    ASSERT_EQ(svgReq.programs.size(), 1u);
    EXPECT_EQ(svgReq.programs[0].first, "inkscape");

    auto nom = writeFile("nom.tex",
        "\\documentclass{article}\n\\usepackage{nomencl}\n\\makenomenclature\n");
    auto nomReq = maketex::analyzeRequirements(nom);
    ASSERT_EQ(nomReq.programs.size(), 1u);
    EXPECT_EQ(nomReq.programs[0].first, "makeindex");

    auto b2g = writeFile("b2g.tex",
        "\\documentclass{article}\n\\usepackage[record]{glossaries-extra}\n"
        "\\GlsXtrLoadResources[src={terms}]\n");
    auto b2gReq = maketex::analyzeRequirements(b2g);
    ASSERT_EQ(b2gReq.programs.size(), 1u);
    EXPECT_EQ(b2gReq.programs[0].first, "bib2gls");
}
