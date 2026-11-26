// End-to-end builds of every testdata/ fixture with the real TeX engines.
// A case skips (visibly) when its engine, tools, or packages are missing —
// a BasicTeX install lacks biblatex, glossaries, minted, and pythontex.

#include "FixtureHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <ostream>
#include <string>
#include <vector>

using namespace maketex;
using namespace maketex::integration;
namespace fs = std::filesystem;

namespace {

struct FixtureCase {
    std::string name;
    EngineKind engine = EngineKind::PdfLaTeX;
    std::vector<std::string> programs; // besides the engine
    std::vector<std::string> texFiles; // packages the document loads
    std::vector<std::string> tools; // tools expected on a cold build
    int coldPasses = 1; // engine passes for a cold build (fixture header)
    bool shellEscape = false;
    // Non-empty while a tracked maketex bug breaks this case; the case is
    // skipped with this text so it stays visible in every test run.
    std::string knownIssue;

    // Names the case in gtest output instead of a byte dump.
    friend void PrintTo(const FixtureCase& c, std::ostream* os) { *os << c.name; }
};

std::string engineProgram(EngineKind kind)
{
    switch (kind) {
    case EngineKind::PdfLaTeX:
        return "pdflatex";
    case EngineKind::XeLaTeX:
        return "xelatex";
    case EngineKind::LuaLaTeX:
        return "lualatex";
    }
    return { };
}

const std::vector<FixtureCase> kCases = {
    { .name = "pdflatex-simple" },
    { .name = "cross-references", .coldPasses = 2 },
    { .name = "toc", .coldPasses = 2 },
    { .name = "input-include", .coldPasses = 2 },
    { .name = "graphics-raster", .texFiles = { "graphicx.sty" } },
    { .name = "graphics-eps", .texFiles = { "epstopdf.sty" } },
    { .name = "pgfplots", .texFiles = { "pgfplots.sty" } },
    { .name = "bibtex",
        .programs = { "bibtex" },
        .tools = { "bibtex" },
        .coldPasses = 3 },
    { .name = "biber",
        .programs = { "biber" },
        .texFiles = { "biblatex.sty" },
        .tools = { "biber" },
        .coldPasses = 3 },
    { .name = "makeindex",
        .programs = { "makeindex" },
        .texFiles = { "makeidx.sty" },
        .tools = { "makeindex" },
        // The index adds a page, so pass 2 rewrites \@abspage@last in the
        // .aux and a third pass is needed for "page X of Y" to be right.
        .coldPasses = 3 },
    { .name = "glossaries",
        .programs = { "makeglossaries" },
        .texFiles = { "glossaries.sty" },
        .tools = { "makeglossaries" },
        .coldPasses = 2 },
    { .name = "pythontex",
        .programs = { "pythontex" },
        .texFiles = { "pythontex.sty" },
        .tools = { "pythontex" },
        .coldPasses = 2 },
    { .name = "minted",
        .programs = { "pygmentize" },
        .texFiles = { "minted.sty" },
        .shellEscape = true },
    { .name = "xelatex-fontspec",
        .engine = EngineKind::XeLaTeX,
        .texFiles = { "fontspec.sty", "lmroman10-regular.otf" } },
    { .name = "lualatex-luacode",
        .engine = EngineKind::LuaLaTeX,
        .texFiles = { "luacode.sty" } },
};

class FixtureTest : public ::testing::TestWithParam<FixtureCase> {
protected:
    void SetUp() override
    {
        const auto& c = GetParam();
        if (!c.knownIssue.empty())
            GTEST_SKIP() << "known issue: " << c.knownIssue;
        std::vector<std::string> missing;
        auto programs = c.programs;
        programs.push_back(engineProgram(c.engine));
        for (const auto& p : programs)
            if (!hasProgram(p))
                missing.push_back(p);
        for (const auto& f : c.texFiles)
            if (!hasTexFile(f))
                missing.push_back(f);
        if (!missing.empty()) {
            std::string list;
            for (const auto& m : missing)
                list += (list.empty() ? "" : ", ") + m;
            GTEST_SKIP() << "not installed: " << list;
        }
    }

    BuildOptions optionsFor(const FixtureCopy& fixture) const
    {
        auto options = fixture.options();
        options.engine = GetParam().engine;
        options.shellEscape = GetParam().shellEscape;
        return options;
    }
};

TEST_P(FixtureTest, coldBuildSucceedsThenIsUpToDate)
{
    FixtureCopy fixture(GetParam().name);

    auto cold = fixture.build(optionsFor(fixture));
    ASSERT_TRUE(cold.result.success) << cold.result.lastError;
    std::string tools;
    for (const auto& t : cold.toolsRun)
        tools += (tools.empty() ? "" : ", ") + t;
    std::string reasons;
    for (const auto& r : cold.result.rerunReasons)
        reasons += "\n  " + r;
    EXPECT_EQ(cold.result.passes, GetParam().coldPasses) << "tools run: " << tools << reasons;
    EXPECT_TRUE(fs::exists(fixture.path("main.pdf")))
        << "PDF not published beside main.tex";
    for (const auto& tool : GetParam().tools)
        EXPECT_NE(std::ranges::find(cold.toolsRun, tool), cold.toolsRun.end())
            << tool << " did not run";

    auto warm = fixture.build(optionsFor(fixture));
    ASSERT_TRUE(warm.result.success) << warm.result.lastError;
    EXPECT_TRUE(warm.upToDate);
    EXPECT_EQ(warm.result.passes, 0);
    EXPECT_TRUE(warm.toolsRun.empty());
}

// ── Incremental rebuilds ─────────────────────────────────────────────────────
// Scenarios a latexmk user hits every day; each pins the number of engine
// passes and tool runs so a regression in scheduling shows up as a number.

class IncrementalTest : public ::testing::Test {
protected:
    void requirePrograms(std::initializer_list<const char*> programs)
    {
        for (const char* p : programs)
            if (!hasProgram(p))
                GTEST_SKIP() << "not installed: " << p;
    }

    static bool ran(const BuildRun& run, const std::string& tool)
    {
        return std::ranges::find(run.toolsRun, tool) != run.toolsRun.end();
    }
};

TEST_F(IncrementalTest, proseEditInBibtexDocumentIsOnePassWithoutBibtex)
{
    requirePrograms({ "pdflatex", "bibtex" });
    FixtureCopy fixture("bibtex");
    ASSERT_TRUE(fixture.build().result.success);

    fixture.replace("main.tex", "\\end{document}", "One more sentence.\n\\end{document}");
    auto run = fixture.build();

    ASSERT_TRUE(run.result.success) << run.result.lastError;
    EXPECT_EQ(run.result.passes, 1);
    EXPECT_FALSE(ran(run, "bibtex"));
}

TEST_F(IncrementalTest, bibCommentEditRerunsBibtexButNotLatex)
{
    requirePrograms({ "pdflatex", "bibtex" });
    FixtureCopy fixture("bibtex");
    ASSERT_TRUE(fixture.build().result.success);

    fixture.append("refs.bib", "\n% a comment bibtex ignores\n");
    auto run = fixture.build();

    ASSERT_TRUE(run.result.success) << run.result.lastError;
    EXPECT_TRUE(ran(run, "bibtex"));
    EXPECT_EQ(run.result.passes, 0);
}

TEST_F(IncrementalTest, bibEntryEditRebuildsBibliography)
{
    requirePrograms({ "pdflatex", "bibtex" });
    FixtureCopy fixture("bibtex");
    ASSERT_TRUE(fixture.build().result.success);
    auto bblBefore = fixture.path(".maketex/main.bbl");
    ASSERT_TRUE(fs::exists(bblBefore));

    fixture.replace("refs.bib", "Literate Programming", "Literate Programming, Revisited");
    auto run = fixture.build();

    ASSERT_TRUE(run.result.success) << run.result.lastError;
    EXPECT_TRUE(ran(run, "bibtex"));
    EXPECT_GE(run.result.passes, 1);
    EXPECT_LE(run.result.passes, 2);
}

TEST_F(IncrementalTest, proseEditInIndexedDocumentIsOnePassWithoutMakeindex)
{
    requirePrograms({ "pdflatex", "makeindex" });
    FixtureCopy fixture("makeindex");
    ASSERT_TRUE(fixture.build().result.success);

    fixture.replace("main.tex", "\\printindex", "One more sentence.\n\n\\printindex");
    auto run = fixture.build();

    ASSERT_TRUE(run.result.success) << run.result.lastError;
    EXPECT_EQ(run.result.passes, 1);
    EXPECT_FALSE(ran(run, "makeindex"));
}

TEST_F(IncrementalTest, bibliographyInIncludedFileRunsBibtex)
{
    requirePrograms({ "pdflatex", "bibtex" });
    FixtureCopy fixture("bibtex");
    fixture.write("main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\include{chap}\n\\end{document}\n");
    fixture.write("chap.tex",
        "See \\cite{knuth1984literate}.\n\\bibliographystyle{plain}\n\\bibliography{refs}\n");
    auto run = fixture.build();

    ASSERT_TRUE(run.result.success) << run.result.lastError;
    EXPECT_TRUE(ran(run, "bibtex"));
    EXPECT_TRUE(fs::exists(fixture.path(".maketex/main.bbl")));
}

TEST_F(IncrementalTest, nonConvergedBuildIsRebuiltNextTime)
{
    requirePrograms({ "pdflatex" });
    FixtureCopy fixture("cross-references");
    auto options = fixture.options();
    options.maxPasses = 1;
    auto first = fixture.build(options);
    ASSERT_FALSE(first.result.success);

    auto second = fixture.build();
    ASSERT_TRUE(second.result.success) << second.result.lastError;
    EXPECT_FALSE(second.upToDate);
    EXPECT_GE(second.result.passes, 1);
}

TEST_F(IncrementalTest, jobnameInExtraOptionsIsHonoured)
{
    requirePrograms({ "pdflatex" });
    FixtureCopy fixture("cross-references");
    auto options = fixture.options();
    options.engineArguments.extraOptions = { "-jobname=paper" };

    auto cold = fixture.build(options);
    ASSERT_TRUE(cold.result.success) << cold.result.lastError;
    EXPECT_EQ(cold.result.passes, 2);
    EXPECT_TRUE(fs::exists(fixture.path("paper.pdf")));

    auto warm = fixture.build(options);
    EXPECT_TRUE(warm.upToDate);
}

// ── Zero-touch ──────────────────────────────────────────────────────────────

TEST_F(IncrementalTest, unicodeDocumentPicksItsEngineUnasked)
{
    requirePrograms({ "lualatex" });
    if (!hasTexFile("fontspec.sty") || !hasTexFile("lmroman10-regular.otf"))
        GTEST_SKIP() << "not installed: fontspec / Latin Modern OpenType";
    FixtureCopy fixture("xelatex-fontspec");
    auto run = fixture.build(); // no engine set

    ASSERT_TRUE(run.result.success) << run.result.lastError;
    EXPECT_EQ(run.result.engine, EngineKind::LuaLaTeX);
    EXPECT_EQ(run.result.engineReason, "loads fontspec");
    EXPECT_FALSE(run.result.retriedEngine);
}

TEST_F(IncrementalTest, engineRequirementHiddenInAClassIsRetried)
{
    // The preamble scan cannot see into a class; the engine's own error
    // tells maketex to switch.
    requirePrograms({ "pdflatex", "lualatex" });
    if (!hasTexFile("fontspec.sty") || !hasTexFile("lmroman10-regular.otf"))
        GTEST_SKIP() << "not installed: fontspec / Latin Modern OpenType";
    FixtureCopy fixture("pdflatex-simple");
    fixture.write("unicodeart.cls",
        "\\NeedsTeXFormat{LaTeX2e}\\ProvidesClass{unicodeart}\n"
        "\\LoadClass{article}\n\\RequirePackage{fontspec}\n"
        "\\setmainfont{lmroman10-regular.otf}\n");
    fixture.write("main.tex",
        "\\documentclass{unicodeart}\n\\begin{document}Hello\\end{document}\n");
    auto run = fixture.build();

    ASSERT_TRUE(run.result.success) << run.result.lastError;
    EXPECT_EQ(run.result.engine, EngineKind::LuaLaTeX);
    EXPECT_TRUE(run.result.retriedEngine);
    // Retry must not emit a bogus "stored build state was incompatible" notice.
    for (const auto& d : run.result.diagnostics)
        EXPECT_EQ(d.message.find("incompatible"), std::string::npos) << d.message;

    // A second run remembers the discovered Unicode engine and stays up to date
    // without failing pdflatex again.
    auto warm = fixture.build();
    ASSERT_TRUE(warm.result.success) << warm.result.lastError;
    EXPECT_TRUE(warm.upToDate);
    EXPECT_EQ(warm.result.passes, 0);
    EXPECT_EQ(warm.result.engine, EngineKind::LuaLaTeX);
    EXPECT_FALSE(warm.result.retriedEngine);
}

TEST_F(IncrementalTest, epsFigureInNonTexSubdirectoryBuilds)
{
    requirePrograms({ "pdflatex" });
    if (!hasTexFile("epstopdf.sty") || !hasTexFile("graphicx.sty"))
        GTEST_SKIP() << "not installed: epstopdf / graphicx";
    FixtureCopy fixture("graphics-eps");
    // Move figure.eps into a figures/ directory that contains no .tex files.
    fs::create_directories(fixture.path("figures"));
    fs::rename(fixture.path("figure.eps"), fixture.path("figures/figure.eps"));
    fixture.replace("main.tex", "\\includegraphics[width=6cm]{figure}",
        "\\includegraphics[width=6cm]{figures/figure}");

    auto run = fixture.build();
    ASSERT_TRUE(run.result.success) << run.result.lastError;
    EXPECT_TRUE(fs::exists(fixture.path("main.pdf")));
}

TEST_F(IncrementalTest, documentsSharingADirectoryKeepTheirState)
{
    requirePrograms({ "pdflatex" });
    FixtureCopy fixture("pdflatex-simple");
    fixture.write("other.tex", "\\documentclass{article}\n\\begin{document}Other\\end{document}\n");
    auto main = fixture.options("main.tex");
    auto other = fixture.options("other.tex");
    other.synctex = false; // different options → a different fingerprint

    ASSERT_TRUE(fixture.build(main).result.success);
    ASSERT_TRUE(fixture.build(other).result.success);
    auto again = fixture.build(main);
    EXPECT_TRUE(again.upToDate);
    EXPECT_TRUE(again.result.diagnostics.empty()) << again.result.diagnostics.front().message;
}

INSTANTIATE_TEST_SUITE_P(Testdata, FixtureTest, ::testing::ValuesIn(kCases),
    [](const auto& testParam) {
        auto name = testParam.param.name;
        std::ranges::replace(name, '-', '_');
        return name;
    });

} // namespace
