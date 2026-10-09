// End-to-end builds of every testdata/ fixture with the real TeX engines.
// A case skips (visibly) when its engine, tools, or packages are missing —
// a BasicTeX install lacks biblatex, glossaries, minted, and pythontex.

#include "FixtureHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
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
    { .name = "cross-references" },
    { .name = "toc" },
    { .name = "input-include",
        .knownIssue = "\\include{chapters/x} needs outDir/chapters/ (plan #16)" },
    { .name = "graphics-raster", .texFiles = { "graphicx.sty" } },
    { .name = "graphics-eps", .texFiles = { "epstopdf.sty" } },
    { .name = "pgfplots", .texFiles = { "pgfplots.sty" } },
    { .name = "bibtex", .programs = { "bibtex" }, .tools = { "bibtex" } },
    { .name = "biber",
        .programs = { "biber" },
        .texFiles = { "biblatex.sty" },
        .tools = { "biber" } },
    { .name = "makeindex",
        .programs = { "makeindex" },
        .texFiles = { "makeidx.sty" },
        .tools = { "makeindex" } },
    { .name = "glossaries",
        .programs = { "makeglossaries" },
        .texFiles = { "glossaries.sty" },
        .tools = { "makeglossaries" } },
    { .name = "pythontex",
        .programs = { "pythontex" },
        .texFiles = { "pythontex.sty" },
        .tools = { "pythontex" } },
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
    EXPECT_GE(cold.result.passes, 1);
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

INSTANTIATE_TEST_SUITE_P(Testdata, FixtureTest, ::testing::ValuesIn(kCases),
    [](const auto& info) {
        auto name = info.param.name;
        std::ranges::replace(name, '-', '_');
        return name;
    });

} // namespace
