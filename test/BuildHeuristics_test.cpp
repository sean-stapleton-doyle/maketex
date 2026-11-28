#include "maketex/build/BuildHeuristics.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <regex>
#include <string>
#include <utility>
#include <vector>

using maketex::Diagnostic;
using maketex::EngineKind;
using maketex::build::chooseDriverModes;
using maketex::build::likelyMultiPassColdBuild;
using maketex::build::likelyMultiPassText;
using maketex::build::mergeContentFilters;
using maketex::build::unicodeEngineNeeded;

namespace {

class BuildHeuristicsTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_heuristics_test"; }
};

Diagnostic makeDiag(std::string msg)
{
    return Diagnostic {
        .severity = Diagnostic::Severity::Error,
        .message = std::move(msg),
    };
}

} // namespace

TEST(BuildHeuristicsPureTest, likelyMultiPassTextDetectsTriggersAndIgnoresComments)
{
    EXPECT_FALSE(likelyMultiPassText("\\documentclass{article}\n\\begin{document}Hi\\end{document}\n"));
    EXPECT_FALSE(likelyMultiPassText("% \\tableofcontents\n\\begin{document}Hi\\end{document}\n"));

    for (const char* trigger : {
             "\\tableofcontents",
             "\\listoffigures",
             "\\listoftables",
             "\\bibliography{refs}",
             "\\usepackage{biblatex}",
             "\\makeindex",
             "\\makeglossaries",
             "\\makenomenclature",
             "\\usepackage{pythontex}",
         }) {
        EXPECT_TRUE(likelyMultiPassText(trigger)) << "trigger: " << trigger;
    }
}

TEST(BuildHeuristicsPureTest, likelyMultiPassTextRequiresBothLabelAndRef)
{
    EXPECT_FALSE(likelyMultiPassText("\\label{sec:intro}"));
    EXPECT_FALSE(likelyMultiPassText("\\ref{sec:intro}"));
    EXPECT_TRUE(likelyMultiPassText("\\label{sec:a}\n\\ref{sec:a}"));
    EXPECT_TRUE(likelyMultiPassText("\\label{sec:a}\n\\pageref{sec:a}"));
    EXPECT_TRUE(likelyMultiPassText("\\label{sec:a}\n\\cref{sec:a}"));
    EXPECT_TRUE(likelyMultiPassText("\\label{sec:a}\n\\autoref{sec:a}"));
}

TEST(BuildHeuristicsPureTest, likelyMultiPassTextHonors256KiBCap)
{
    std::string padding(256 * 1024, 'x');
    EXPECT_FALSE(likelyMultiPassText(padding + "\n\\tableofcontents\n"));
    EXPECT_TRUE(likelyMultiPassText("\\tableofcontents\n" + padding));
}

TEST_F(BuildHeuristicsTest, likelyMultiPassColdBuildChecksAuxAndFile)
{
    auto tex = writeFile("main.tex", "\\documentclass{article}\n\\tableofcontents\n");
    auto aux = m_dir / "main.aux";

    EXPECT_TRUE(likelyMultiPassColdBuild(tex, aux));
    EXPECT_FALSE(likelyMultiPassColdBuild(m_dir / "missing.tex", aux));

    writeFile("main.aux", "\\relax\n");
    EXPECT_FALSE(likelyMultiPassColdBuild(tex, aux));
}

TEST(BuildHeuristicsPureTest, unicodeEngineNeededPrefersLuaLatexOverXeTex)
{
    EXPECT_FALSE(unicodeEngineNeeded({ }).has_value());
    {
        std::vector<Diagnostic> diags { makeDiag("Undefined control sequence") };
        EXPECT_FALSE(unicodeEngineNeeded(diags).has_value());
    }

    // "requires XeTeX or LuaTeX" contains "requires XeTeX", so LuaLaTeX must win.
    for (const char* msg : {
             "fontspec requires either XeTeX or LuaTeX",
             "This package requires XeLaTeX or LuaLaTeX",
             "Package foo requires XeTeX or LuaTeX to compile",
             "Cannot be run with pdfLaTeX",
             "Package bar requires LuaTeX",
             "LuaTeX is required for graphdrawing",
             "Please run LuaTeX instead",
             "You need LuaTeX",
             "Document requires LuaLaTeX",
             "LuaLaTeX is required",
         }) {
        std::vector<Diagnostic> diags { makeDiag(msg) };
        EXPECT_EQ(unicodeEngineNeeded(diags), EngineKind::LuaLaTeX) << msg;
    }

    for (const char* msg : {
             "Package xecjk requires XeTeX",
             "XeTeX is required here",
             "Package poly requires XeLaTeX",
             "XeLaTeX is required",
         }) {
        std::vector<Diagnostic> diags { makeDiag(msg) };
        EXPECT_EQ(unicodeEngineNeeded(diags), EngineKind::XeLaTeX) << msg;
    }
}

TEST(BuildHeuristicsPureTest, chooseDriverModesMatrix)
{
    auto xeWithSplit = chooseDriverModes(EngineKind::XeLaTeX, 8, true, true);
    EXPECT_TRUE(xeWithSplit.xelatexXdvSplit);
    EXPECT_FALSE(xeWithSplit.draftFirstPass);

    auto xeNoSplit = chooseDriverModes(EngineKind::XeLaTeX, 8, false, true);
    EXPECT_FALSE(xeNoSplit.xelatexXdvSplit);
    EXPECT_FALSE(xeNoSplit.draftFirstPass);

    auto pdfCold = chooseDriverModes(EngineKind::PdfLaTeX, 2, true, true);
    EXPECT_FALSE(pdfCold.xelatexXdvSplit);
    EXPECT_TRUE(pdfCold.draftFirstPass);

    auto luaCold = chooseDriverModes(EngineKind::LuaLaTeX, 4, false, true);
    EXPECT_FALSE(luaCold.xelatexXdvSplit);
    EXPECT_TRUE(luaCold.draftFirstPass);

    auto singlePass = chooseDriverModes(EngineKind::PdfLaTeX, 1, false, true);
    EXPECT_FALSE(singlePass.draftFirstPass);

    auto warm = chooseDriverModes(EngineKind::PdfLaTeX, 8, false, false);
    EXPECT_FALSE(warm.draftFirstPass);
}

TEST(BuildHeuristicsPureTest, mergeContentFiltersPreservesBuiltInDefaults)
{
    auto defaults = mergeContentFilters({ });
    ASSERT_EQ(defaults.size(), 2u);

    std::vector<std::pair<std::string, std::string>> user {
        { "aux", R"(^\\custom\{)" },
        { "aux", R"(^\\extra\{)" },
        { "toc", R"(^% timestamp)" },
    };
    auto merged = mergeContentFilters(user);
    ASSERT_EQ(merged.size(), 3u);

    auto findFilter = [&](std::string_view ext) -> const std::regex* {
        for (const auto& [k, re] : merged)
            if (k == ext)
                return &re;
        return nullptr;
    };

    const auto* auxRe = findFilter("aux");
    ASSERT_NE(auxRe, nullptr);
    EXPECT_TRUE(std::regex_search(R"(\@writefile{toc}{...})", *auxRe));
    EXPECT_TRUE(std::regex_search(R"(\custom{foo})", *auxRe));
    EXPECT_TRUE(std::regex_search(R"(\extra{bar})", *auxRe));
    EXPECT_FALSE(std::regex_search(R"(\newlabel{sec:1})", *auxRe));

    const auto* epsRe = findFilter("eps");
    ASSERT_NE(epsRe, nullptr);
    EXPECT_TRUE(std::regex_search("%%CreationDate: 2026-01-01", *epsRe));

    const auto* tocRe = findFilter("toc");
    ASSERT_NE(tocRe, nullptr);
    EXPECT_TRUE(std::regex_search("% timestamp 123", *tocRe));
}
