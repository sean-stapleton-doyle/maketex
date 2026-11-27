#include "render/InlineReport.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <sstream>
#include <string>

using maketex::BuildOptions;
using maketex::BuildResult;
using maketex::Diagnostic;
using maketex::DriverResult;
using maketex::EngineKind;
using maketex::engineKindName;
using maketex::cli::buildReport;
using maketex::cli::diagnosticRow;
using maketex::cli::engineLabel;
using maketex::cli::printHumanReport;
using maketex::cli::printInline;
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

TEST(InlineReport, useStyledOutputTruthTable)
{
    EXPECT_TRUE(useStyledOutput(/*plainFlag=*/false, /*isTty=*/true, /*noColor=*/false));
    EXPECT_FALSE(useStyledOutput(/*plainFlag=*/true, /*isTty=*/true, /*noColor=*/false));
    EXPECT_FALSE(useStyledOutput(/*plainFlag=*/false, /*isTty=*/false, /*noColor=*/false));
    EXPECT_FALSE(useStyledOutput(/*plainFlag=*/false, /*isTty=*/true, /*noColor=*/true));
    EXPECT_FALSE(useStyledOutput(/*plainFlag=*/true, /*isTty=*/false, /*noColor=*/true));
}

TEST(InlineReport, engineLabelWithAndWithoutReason)
{
    EXPECT_EQ(engineLabel(EngineKind::PdfLaTeX, ""), "pdflatex");
    EXPECT_EQ(engineLabel(EngineKind::XeLaTeX, "loads fontspec"), "xelatex (loads fontspec)");
}

TEST(InlineReport, severityBadgesForWarningAndInfo)
{
    auto warn = renderToString(diagnosticRow(
        Diagnostic { .severity = Diagnostic::Severity::Warning, .message = "Overfull hbox" },
        false));
    EXPECT_NE(warn.find("warn"), std::string::npos);
    EXPECT_NE(warn.find("Overfull hbox"), std::string::npos);

    auto info = renderToString(diagnosticRow(
        Diagnostic { .severity = Diagnostic::Severity::Info, .message = "Installed foo" },
        false));
    EXPECT_NE(info.find("info"), std::string::npos);
    EXPECT_NE(info.find("Installed foo"), std::string::npos);
}

TEST(InlineReport, diagnosticRowHandlesFileWithoutLineAndMultilineContextAndHint)
{
    auto dir = maketex::test_utils::uniqueTempDir("maketex_inline_snippet");
    std::filesystem::create_directories(dir);
    auto src = dir / "chapter.tex";
    std::ofstream(src) << "line 1\n\\badcommand here\nline 3\n";

    Diagnostic d {
        .severity = Diagnostic::Severity::Error,
        .file = "chapter.tex",
        .sourcePath = src,
        .line = 2,
        .message = "Undefined control sequence",
        .rawContext = "first context line\nsecond context line",
        .hint = "check spelling",
    };

    auto verbose = renderToString(diagnosticRow(d, /*verbose=*/true));
    EXPECT_NE(verbose.find("\\badcommand here"), std::string::npos);
    EXPECT_NE(verbose.find("hint:"), std::string::npos);
    EXPECT_NE(verbose.find("check spelling"), std::string::npos);
    EXPECT_NE(verbose.find("first context line"), std::string::npos);
    EXPECT_NE(verbose.find("second context line"), std::string::npos);

    // File with line == 0 prints file without ":0".
    Diagnostic noLine {
        .severity = Diagnostic::Severity::Warning,
        .file = "refs.bib",
        .line = 0,
        .message = "Warning--empty year",
    };
    auto noLineRendered = renderToString(diagnosticRow(noLine, false));
    EXPECT_NE(noLineRendered.find("refs.bib"), std::string::npos);
    EXPECT_EQ(noLineRendered.find("refs.bib:0"), std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST(InlineReport, buildReportCoversToolsOnlyDiagnosticsAndVerboseRerunReasons)
{
    auto result = fromDriver(DriverResult {
        .success = true,
        .passes = 0,
        .upToDate = false,
        .rerunReasons = { "pass 2: main.aux changed" },
        .diagnostics = { Diagnostic { .severity = Diagnostic::Severity::Warning, .message = "cite missing" } },
    });
    BuildOptions options { .texFile = "/project/paper.tex" };

    auto rendered = renderToString(buildReport(result, options, /*verbose=*/true));
    EXPECT_NE(rendered.find("tools only, no engine pass needed"), std::string::npos);
    EXPECT_NE(rendered.find("cite missing"), std::string::npos);
    EXPECT_NE(rendered.find("pass 2: main.aux changed"), std::string::npos);
}

TEST(InlineReport, printInlineWritesRenderedDocumentToStream)
{
    auto result = fromDriver(DriverResult { .success = true, .passes = 1 });
    BuildOptions options { .texFile = "/project/doc.tex" };
    std::ostringstream oss;

    printInline(buildReport(result, options, false), oss);

    auto output = oss.str();
    EXPECT_NE(output.find("doc.tex"), std::string::npos);
    EXPECT_NE(output.find("1 pass"), std::string::npos);
    EXPECT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
}

TEST(InlineReport, printHumanReportPlainAndStyledVariants)
{
    auto dir = maketex::test_utils::uniqueTempDir("maketex_human_report");
    std::filesystem::create_directories(dir);
    auto src = dir / "main.tex";
    std::ofstream(src) << "\\documentclass{article}\n\\oops\n";

    BuildOptions options { .texFile = src };

    // Plain failure with snippet, hint, verbose rerun reason, and engine reason.
    {
        auto result = fromDriver(DriverResult {
            .success = false,
            .passes = 2,
            .rerunReasons = { "pass 2: aux changed" },
            .lastError = "pdflatex exited with code 1",
            .diagnostics = { Diagnostic {
                .severity = Diagnostic::Severity::Error,
                .file = "main.tex",
                .sourcePath = src,
                .line = 2,
                .message = "Undefined control sequence",
                .hint = "define \\oops",
            } },
        });
        result.engine = EngineKind::XeLaTeX;
        result.engineReason = "loads fontspec";

        std::ostringstream out;
        std::ostringstream err;
        printHumanReport(result, options, /*verbose=*/true, /*styled=*/false, out, err);

        EXPECT_TRUE(out.str().empty());
        EXPECT_NE(err.str().find("\\oops"), std::string::npos);
        EXPECT_NE(err.str().find("hint: define \\oops"), std::string::npos);
        EXPECT_NE(err.str().find("maketex: pass 2: aux changed"), std::string::npos);
        EXPECT_NE(err.str().find("maketex: engine xelatex (loads fontspec)"), std::string::npos);
        EXPECT_NE(err.str().find("maketex: failed after 2 passes: pdflatex exited with code 1"),
            std::string::npos);
    }

    // Plain up-to-date, tools-only, and normal success.
    {
        std::ostringstream out;
        std::ostringstream err;
        printHumanReport(fromDriver(DriverResult { .success = true, .upToDate = true }),
            options, false, false, out, err);
        EXPECT_EQ(out.str(), "maketex: up to date\n");
    }
    {
        std::ostringstream out;
        std::ostringstream err;
        printHumanReport(fromDriver(DriverResult { .success = true, .passes = 0 }),
            options, false, false, out, err);
        EXPECT_EQ(out.str(), "maketex: done (tools only, no engine pass needed)\n");
    }
    {
        std::ostringstream out;
        std::ostringstream err;
        printHumanReport(fromDriver(DriverResult { .success = true, .passes = 1 }),
            options, false, false, out, err);
        EXPECT_EQ(out.str(), "maketex: done in 1 pass\n");
    }

    // Styled output goes to out, leaving err empty.
    {
        std::ostringstream out;
        std::ostringstream err;
        printHumanReport(fromDriver(DriverResult { .success = true, .passes = 1 }),
            options, false, /*styled=*/true, out, err);
        EXPECT_NE(out.str().find("main.tex"), std::string::npos);
        EXPECT_TRUE(err.str().empty());
    }

    std::filesystem::remove_all(dir);
}
