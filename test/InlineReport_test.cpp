#include "render/InlineReport.h"

#include <gtest/gtest.h>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <string>

using maketex::BuildOptions;
using maketex::BuildResult;
using maketex::Diagnostic;
using maketex::DriverResult;
using maketex::EngineKind;
using maketex::engineKindName;
using maketex::cli::buildReport;
using maketex::cli::diagnosticRow;
using maketex::cli::useStyledOutput;

namespace {

BuildResult fromDriver(const DriverResult& driver)
{
    BuildResult result;
    static_cast<DriverResult&>(result) = driver;
    return result;
}

std::string renderToString(ftxui::Element element)
{
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
        ftxui::Dimension::Fit(element));
    ftxui::Render(screen, element);
    return screen.ToString();
}

} // namespace

TEST(InlineReport, successReportShowsFileEngineAndPasses)
{
    auto result = fromDriver(DriverResult { .success = true, .passes = 2 });
    result.engine = EngineKind::LuaLaTeX;
    result.engineReason = "loads fontspec";
    BuildOptions options { .texFile = "/tmp/thesis.tex" };

    auto rendered = renderToString(buildReport(result, options, false));
    EXPECT_NE(rendered.find("thesis.tex"), std::string::npos);
    EXPECT_NE(rendered.find("lualatex (loads fontspec)"), std::string::npos);
    EXPECT_NE(rendered.find("2 passes"), std::string::npos);
    EXPECT_NE(rendered.find("built"), std::string::npos);
}

TEST(InlineReport, upToDateReadsUpToDate)
{
    auto result = fromDriver(DriverResult { .success = true, .passes = 0, .upToDate = true });
    BuildOptions options { .texFile = "/tmp/thesis.tex" };

    auto rendered = renderToString(buildReport(result, options, false));
    EXPECT_NE(rendered.find("up to date"), std::string::npos);
    EXPECT_EQ(rendered.find("0 passes"), std::string::npos);
}

TEST(InlineReport, failureReportShowsError)
{
    auto result = fromDriver(DriverResult { .success = false,
        .passes = 1,
        .lastError = "latex exited with code 1" });
    BuildOptions options { .texFile = "/tmp/thesis.tex" };

    auto rendered = renderToString(buildReport(result, options, false));
    EXPECT_NE(rendered.find("failed"), std::string::npos);
    EXPECT_NE(rendered.find("latex exited with code 1"), std::string::npos);
}

TEST(InlineReport, diagnosticRowShowsLocationAndMessage)
{
    Diagnostic diagnostic {
        .severity = Diagnostic::Severity::Error,
        .file = "chapter.tex",
        .line = 42,
        .message = "Undefined control sequence",
        .rawContext = "l.42 \\foo",
    };

    auto rendered = renderToString(diagnosticRow(diagnostic, false));
    EXPECT_NE(rendered.find("error"), std::string::npos);
    EXPECT_NE(rendered.find("chapter.tex:42"), std::string::npos);
    EXPECT_NE(rendered.find("Undefined control sequence"), std::string::npos);
    EXPECT_EQ(rendered.find("l.42"), std::string::npos);

    auto verbose = renderToString(diagnosticRow(diagnostic, true));
    EXPECT_NE(verbose.find("l.42"), std::string::npos);
}

TEST(InlineReport, engineNames)
{
    EXPECT_EQ(engineKindName(EngineKind::PdfLaTeX), "pdflatex");
    EXPECT_EQ(engineKindName(EngineKind::XeLaTeX), "xelatex");
    EXPECT_EQ(engineKindName(EngineKind::LuaLaTeX), "lualatex");
}

TEST(InlineReport, plainFlagAlwaysDisablesStyling)
{
    EXPECT_FALSE(useStyledOutput(/*plainFlag=*/true));
}
