#include "maketex/DiagnosticExtractor.h"

#include <gtest/gtest.h>

#include <string>

using namespace maketex;

namespace {

RunResult<EngineInvocation> latexResult(std::string stdoutOutput, int exitCode)
{
    return RunResult<EngineInvocation> {
        .invocation = EngineInvocation { .texFile = "main.tex" },
        .kind = InvokerKind::PdfLaTeX,
        .exitCode = exitCode,
        .stdoutOutput = std::move(stdoutOutput),
    };
}

} // namespace

TEST(DiagnosticExtractor, extractsErrorWithLineNumber)
{
    auto diags = diagnostics::extract(latexResult(
        "! Undefined control sequence.\n"
        "l.42 \\badmacro\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[0].message, "Undefined control sequence");
    EXPECT_EQ(diags[0].line, 42);
    EXPECT_EQ(diags[0].file, "main.tex");
}

TEST(DiagnosticExtractor, errorInsideLookAheadWindowIsNotSwallowed)
{
    // The l.N look-ahead must not consume a following error line: both
    // errors here sit within 8 lines of each other.
    auto diags = diagnostics::extract(latexResult(
        "! Undefined control sequence.\n"
        "l.10 \\foo\n"
        "noise\n"
        "! Missing $ inserted.\n"
        "l.12 x_2\n",
        1));

    ASSERT_EQ(diags.size(), 2u);
    EXPECT_EQ(diags[0].message, "Undefined control sequence");
    EXPECT_EQ(diags[0].line, 10);
    EXPECT_EQ(diags[1].message, "Missing $ inserted");
    EXPECT_EQ(diags[1].line, 12);
}

TEST(DiagnosticExtractor, backToBackErrorsAreBothReported)
{
    auto diags = diagnostics::extract(latexResult(
        "! First error.\n"
        "! Second error.\n",
        1));

    ASSERT_EQ(diags.size(), 2u);
    EXPECT_EQ(diags[0].message, "First error");
    EXPECT_EQ(diags[1].message, "Second error");
}

TEST(DiagnosticExtractor, stripsLatexErrorPrefix)
{
    auto diags = diagnostics::extract(latexResult(
        "! LaTeX Error: File `missing.sty' not found.\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].message, "File `missing.sty' not found");
}

TEST(DiagnosticExtractor, failureWithoutStructuredErrorsYieldsFallback)
{
    auto diags = diagnostics::extract(latexResult("segfault or similar\n", 1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[0].rawContext, "segfault or similar\n");
}

TEST(DiagnosticExtractor, successExtractsWarnings)
{
    auto diags = diagnostics::extract(latexResult(
        "LaTeX Warning: Reference `fig:one' on page 1 undefined on input line 7.\n",
        0));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[0].line, 7);
}

TEST(DiagnosticExtractor, biberToolErrors)
{
    RunResult<ToolInvocation> result {
        .invocation = ToolInvocation { .jobName = "main" },
        .kind = InvokerKind::Biber,
        .exitCode = 2,
        .stdoutOutput = "INFO - This is Biber 2.19\n"
                        "ERROR - Cannot find file 'refs.bib'\n",
    };

    auto diags = diagnostics::extract(result);
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[0].message, "Cannot find file 'refs.bib'");
}

// ── file:line: error markers (-file-line-error) ──────────────────────────────

TEST(DiagnosticExtractor, fileLineErrorMarkerIsParsed)
{
    auto diags = diagnostics::extract(latexResult(
        "./chapter.tex:12: Undefined control sequence.\n"
        "l.12 \\badmacro\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[0].file, "chapter.tex");
    EXPECT_EQ(diags[0].line, 12);
    EXPECT_EQ(diags[0].message, "Undefined control sequence");
    EXPECT_EQ(diags[0].category, "undefined-macro");
}

TEST(DiagnosticExtractor, fileLineErrorStripsLatexErrorPrefix)
{
    auto diags = diagnostics::extract(latexResult(
        "./main.tex:26: LaTeX Error: \\begin{center} ended by \\end{document}.\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].message, "\\begin{center} ended by \\end{document}");
    EXPECT_EQ(diags[0].file, "main.tex");
    EXPECT_EQ(diags[0].line, 26);
}

TEST(DiagnosticExtractor, fileLineErrorMissingFileCategory)
{
    auto diags = diagnostics::extract(latexResult(
        "./main.tex:3: LaTeX Error: File `nope.sty' not found.\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].category, "missing-file");
}

TEST(DiagnosticExtractor, fileLineErrorFontspecIsMissingFont)
{
    auto diags = diagnostics::extract(latexResult(
        "./main.tex:5: Package fontspec Error: The font \"NoSuchFont\" cannot be"
        " found.\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].category, "missing-font");
}

TEST(DiagnosticExtractor, colonInPlainTextIsNotAFileLineError)
{
    // "note: 12: x" has no TeX-ish path before the digits — must not match.
    auto diags = diagnostics::extract(latexResult(
        "some note: 12: nothing\n"
        "! Real error.\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].message, "Real error");
}

// ── warning patterns and categories ──────────────────────────────────────────

TEST(DiagnosticExtractor, citationWarningKeepsKeyAndCategory)
{
    auto diags = diagnostics::extract(latexResult(
        "LaTeX Warning: Citation `knuth1984' on page 1 undefined on input line 9.\n",
        0));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Warning);
    EXPECT_NE(diags[0].message.find("knuth1984"), std::string::npos);
    EXPECT_EQ(diags[0].category, "undefined-citation");
    EXPECT_EQ(diags[0].line, 9);
}

TEST(DiagnosticExtractor, referenceWarningCategory)
{
    auto diags = diagnostics::extract(latexResult(
        "LaTeX Warning: Reference `fig:one' on page 1 undefined on input line 7.\n",
        0));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].category, "undefined-reference");
}

TEST(DiagnosticExtractor, packageWarningFoldsContinuationLines)
{
    auto diags = diagnostics::extract(latexResult(
        "Package hyperref Warning: Token not allowed\n"
        "(hyperref)                in a PDF string on input line 12.\n",
        0));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].message,
        "Package hyperref: Token not allowed in a PDF string");
    EXPECT_EQ(diags[0].line, 12);
    EXPECT_EQ(diags[0].category, "package");
}

TEST(DiagnosticExtractor, missingCharacterIsMissingGlyphWarning)
{
    auto diags = diagnostics::extract(latexResult(
        "Missing character: There is no W in font ec-lmr10!\n",
        0));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[0].category, "missing-glyph");
}

TEST(DiagnosticExtractor, overfullHboxCategory)
{
    auto diags = diagnostics::extract(latexResult(
        "Overfull \\hbox (15.0pt too wide) in paragraph at lines 8--9\n",
        0));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].category, "overfull-hbox");
    EXPECT_EQ(diags[0].line, 8);
}

// ── tool citation categories ──────────────────────────────────────────────────

TEST(DiagnosticExtractor, bibtexMissingEntryIsUndefinedCitation)
{
    RunResult<ToolInvocation> result {
        .invocation = ToolInvocation { .jobName = "main" },
        .kind = InvokerKind::Bibtex,
        .exitCode = 2,
        .stdoutOutput
        = "Warning--I didn't find a database entry for \"missingkey\"\n",
    };

    auto diags = diagnostics::extract(result);
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].category, "undefined-citation");
    EXPECT_NE(diags[0].message.find("missingkey"), std::string::npos);
}

TEST(DiagnosticExtractor, biberMissingEntryIsUndefinedCitation)
{
    RunResult<ToolInvocation> result {
        .invocation = ToolInvocation { .jobName = "main" },
        .kind = InvokerKind::Biber,
        .exitCode = 2,
        .stdoutOutput
        = "ERROR - I didn't find a database entry for 'missingkey'\n",
    };

    auto diags = diagnostics::extract(result);
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].category, "undefined-citation");
}

// ── open-file-stack attribution (needs real files on disk) ───────────────────

#include "test_utils.h"

#include <filesystem>

namespace {

class DiagnosticAttributionTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_diag_attr_test"; }

    RunResult<EngineInvocation> resultIn(std::string stdoutOutput, int exitCode)
    {
        return RunResult<EngineInvocation> {
            .invocation = EngineInvocation { .texFile = m_dir / "main.tex" },
            .kind = InvokerKind::PdfLaTeX,
            .exitCode = exitCode,
            .stdoutOutput = std::move(stdoutOutput),
        };
    }
};

} // namespace

TEST_F(DiagnosticAttributionTest, errorInNestedInputIsAttributedToThatFile)
{
    writeFile("main.tex", "\\input{chapters/ch1}");
    std::filesystem::create_directories(m_dir / "chapters");
    writeFile("chapters/ch1.tex", "\\badmacro");

    auto diags = diagnostics::extract(resultIn(
        "(./main.tex (./chapters/ch1.tex\n"
        "! Undefined control sequence.\n"
        "l.1 \\badmacro\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].file, "chapters/ch1.tex");
    EXPECT_EQ(diags[0].line, 1);
    EXPECT_FALSE(diags[0].sourcePath.empty());
    EXPECT_TRUE(std::filesystem::equivalent(
        diags[0].sourcePath, m_dir / "chapters/ch1.tex"));
}

TEST_F(DiagnosticAttributionTest, closingParenPopsBackToOuterFile)
{
    writeFile("main.tex", "\\input{ch1}");
    writeFile("ch1.tex", "ok");

    auto diags = diagnostics::extract(resultIn(
        "(./main.tex (./ch1.tex)\n"
        "! Misplaced alignment tab character &.\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].file, "main.tex");
}

TEST_F(DiagnosticAttributionTest, nonFileParensDoNotDisturbAttribution)
{
    writeFile("main.tex", "x");
    writeFile("ch1.tex", "y");

    // "(see the transcript)" is balanced text, "(2024/01/01)" a version
    // string — neither names a file, so neither must pop ./ch1.tex.
    auto diags = diagnostics::extract(resultIn(
        "(./main.tex (./ch1.tex\n"
        "some package (2024/01/01) loaded (see the transcript)\n"
        "! Missing $ inserted.\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].file, "ch1.tex");
}

TEST_F(DiagnosticAttributionTest, warningsAreAttributedViaStack)
{
    writeFile("main.tex", "x");
    writeFile("ch2.tex", "y");

    auto diags = diagnostics::extract(resultIn(
        "(./main.tex (./ch2.tex\n"
        "LaTeX Warning: Reference `fig:a' on page 2 undefined on input line 4.\n",
        0));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].file, "ch2.tex");
    EXPECT_EQ(diags[0].line, 4);
    EXPECT_FALSE(diags[0].sourcePath.empty());
}

TEST_F(DiagnosticAttributionTest, rootDocumentSourcePathIsResolved)
{
    writeFile("main.tex", "x");

    auto diags = diagnostics::extract(resultIn(
        "! Emergency stop.\n",
        1));

    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].file, "main.tex");
    EXPECT_TRUE(std::filesystem::equivalent(diags[0].sourcePath, m_dir / "main.tex"));
}

// ── Missing packages, fatal noise, and hints ─────────────────────────────────

TEST(DiagnosticExtractor, packageErrorContinuationLinesAreFolded)
{
    auto diags = diagnostics::extract(latexResult(
        "./main.tex:24: Package fontspec Error: \n"
        "(fontspec)                The font \"Latin Modern Roman\" cannot be found;\n"
        "(fontspec)                this may be but usually is not a fontspec bug.\n"
        "\n"
        "l.24 \\setmainfont{Latin Modern Roman}\n",
        1));
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].message,
        "Package fontspec Error: The font \"Latin Modern Roman\" cannot be found; "
        "this may be but usually is not a fontspec bug");
}

TEST(DiagnosticExtractor, emergencyStopIsDroppedWhenThereIsARealError)
{
    auto diags = diagnostics::extract(latexResult(
        "./main.tex:3: LaTeX Error: File `minted.sty' not found.\n"
        "./main.tex:5: Emergency stop.\n"
        "!  ==> Fatal error occurred, no output PDF file produced!\n",
        1));
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].category, "missing-file");
    EXPECT_EQ(diags[0].missingFile, "minted.sty");
}

TEST(DiagnosticExtractor, nextFileLineErrorEndsTheContext)
{
    auto diags = diagnostics::extract(latexResult(
        "./main.tex:3: Undefined control sequence.\n"
        "./main.tex:9: Missing $ inserted.\n",
        1));
    ASSERT_EQ(diags.size(), 2u);
    EXPECT_EQ(diags[0].rawContext.find("Missing $"), std::string::npos);
}

TEST(DiagnosticExtractor, shellEscapeErrorCarriesAHint)
{
    auto diags = diagnostics::extract(latexResult(
        "./main.tex:20: Package minted Error: You must invoke LaTeX with the -shell-escape flag.\n",
        1));
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].category, "shell-escape-required");
    EXPECT_NE(diags[0].hint.find("--shell-escape"), std::string::npos);
}

TEST(DiagnosticExtractor, cantFindFileIsMissingFile)
{
    auto diags = diagnostics::extract(latexResult(
        "! I can't find file `chapter'.\nl.7 \\input{chapter}\n", 1));
    ASSERT_FALSE(diags.empty());
    EXPECT_EQ(diags[0].category, "missing-file");
    EXPECT_EQ(diags[0].missingFile, "chapter");
}

TEST(DiagnosticExtractor, missingProgramIsMissingTool)
{
    RunResult<ToolInvocation> result {
        .kind = InvokerKind::Biber,
        .exitCode = 127,
        .program = "biber",
        .notFound = true,
    };
    auto diags = diagnostics::extract(result);
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].category, "missing-tool");
    EXPECT_EQ(diags[0].missingFile, "biber");
}

TEST_F(DiagnosticAttributionTest, missingPackageIsAttributedToItsUsepackageLine)
{
    // TeX gives up at a later line (where the package's macro is used, or
    // \begin{document}); the user needs the \usepackage line.
    writeFile("main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{amsmath}\n"
        "\\usepackage[backend=biber]{biblatex, csquotes}\n"
        "\\addbibresource{refs.bib}\n");
    auto diags = diagnostics::extract(resultIn(
        "./main.tex:4: LaTeX Error: File `biblatex.sty' not found.\n", 1));
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].line, 3);
}
