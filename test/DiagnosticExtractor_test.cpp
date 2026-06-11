#include "maketex/DiagnosticExtractor.h"

#include <gtest/gtest.h>

#include <string>

using namespace maketex;

namespace {

RunResult<EngineInvocation> latexResult(std::string stdoutOutput, int exitCode)
{
    return RunResult<EngineInvocation> {
        .invocation = EngineInvocation { .texFile = "main.tex" },
        .kind = InvokerKind::PdfLatex,
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
