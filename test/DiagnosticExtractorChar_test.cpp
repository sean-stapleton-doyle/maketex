#include "maketex/DiagnosticExtractor.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace maketex;

namespace {

RunResult<EngineInvocation> engineRes(std::string out, int exitCode,
    InvokerKind kind = InvokerKind::PdfLaTeX,
    std::filesystem::path texFile = "main.tex")
{
    return RunResult<EngineInvocation> {
        .invocation = EngineInvocation { .texFile = std::move(texFile) },
        .kind = kind,
        .exitCode = exitCode,
        .stdoutOutput = std::move(out),
    };
}

RunResult<ToolInvocation> toolRes(InvokerKind kind, std::string out, int exitCode,
    std::string err = { })
{
    return RunResult<ToolInvocation> {
        .invocation = ToolInvocation { .jobName = "main" },
        .kind = kind,
        .exitCode = exitCode,
        .stdoutOutput = std::move(out),
        .stderrOutput = std::move(err),
    };
}

class DiagnosticExtractorCharFixture : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_diag_char"; }
};

} // namespace

// ── LaTeX errors: forms, context, categories, refinement ─────────────────────

TEST(DiagnosticExtractorChar, latexErrorFormsAndEngineDispatch)
{
    // File:line, bang error, engine error, double-space skips, and continuation folding.
    const std::string log = "line1\nline2\nline3\nline4\nline5\nline6\nline7\nline8\n"
                            "./ch1.tex:10: Undefined control sequence.\n"
                            "l.10 \\oops\n"
                            "! Missing $ inserted.\n"
                            "l.15 a_b\n"
                            "!  ==> Fatal error occurred, no output PDF file produced!\n"
                            "./ch1.tex:16:  ==> Fatal error occurred, no output PDF file produced!\n"
                            "!LuaTeX error: something went wrong.\n"
                            "!notanengineerr without keyword\n"
                            "./ch1.tex:20: Package foo Error: first part\n"
                            "(foo)   \n"
                            "(foo)   second part.\n";

    for (InvokerKind kind : { InvokerKind::PdfLaTeX, InvokerKind::XeLaTeX, InvokerKind::LuaLaTeX }) {
        auto diags = diagnostics::extract(engineRes(log, 1, kind));
        ASSERT_EQ(diags.size(), 4u);

        EXPECT_EQ(diags[0].file, "ch1.tex");
        EXPECT_EQ(diags[0].line, 10);
        EXPECT_EQ(diags[0].message, "Undefined control sequence");
        EXPECT_EQ(diags[0].category, "undefined-macro");
        // Context has up to 7 lines before (line2..line8), the error line, and l.10.
        EXPECT_EQ(diags[0].rawContext.find("line1\n"), std::string::npos);
        EXPECT_NE(diags[0].rawContext.find("line2\n"), std::string::npos);
        EXPECT_NE(diags[0].rawContext.find("l.10 \\oops\n"), std::string::npos);

        EXPECT_EQ(diags[1].file, "main.tex");
        EXPECT_EQ(diags[1].line, 15);
        EXPECT_EQ(diags[1].message, "Missing $ inserted");
        EXPECT_EQ(diags[1].category, "");

        EXPECT_EQ(diags[2].message, "LuaTeX error: something went wrong");
        EXPECT_EQ(diags[2].line, -1);

        EXPECT_EQ(diags[3].file, "ch1.tex");
        EXPECT_EQ(diags[3].line, 20);
        EXPECT_EQ(diags[3].message, "Package foo Error: first part second part");
        EXPECT_EQ(diags[3].category, "package");
    }
}

TEST(DiagnosticExtractorChar, latexErrorCategoriesAndHints)
{
    const std::string log = "./main.tex:1: Font \\foo=bar not loadable: Metric (TFM) file not found.\n"
                            "./main.tex:2: Package x Error: font \"A\" cannot be loaded.\n"
                            "./main.tex:3: Package minted Error: shell escape is disabled.\n"
                            "./main.tex:4: \\write18 disabled.\n"
                            "./main.tex:5: LaTeX Error: File `foo.sty' not found.\n"
                            "./main.tex:6: I can't find file `inc.tex'.\n";

    auto diags = diagnostics::extract(engineRes(log, 1));
    ASSERT_EQ(diags.size(), 6u);

    EXPECT_EQ(diags[0].category, "missing-font");
    EXPECT_EQ(diags[1].category, "missing-font");
    EXPECT_EQ(diags[2].category, "shell-escape-required");
    EXPECT_NE(diags[2].hint.find("--shell-escape"), std::string::npos);
    EXPECT_EQ(diags[3].category, "shell-escape-required");
    EXPECT_FALSE(diags[3].hint.empty());
    EXPECT_EQ(diags[4].category, "missing-file");
    EXPECT_EQ(diags[4].missingFile, "foo.sty");
    EXPECT_EQ(diags[5].category, "missing-file");
    EXPECT_EQ(diags[5].missingFile, "inc.tex");
}

TEST(DiagnosticExtractorChar, latexLookAheadBoundsAndMalformedLineTag)
{
    // l.N 9 lines after error is outside kLookAhead (= 8); l.abc stops look-ahead.
    const std::string log = "! Far line error.\n"
                            "n1\nn2\nn3\nn4\nn5\nn6\nn7\nn8\n"
                            "l.99 too far\n"
                            "! Malformed tag error.\n"
                            "l.abc bad\n"
                            "l.42 unreachable\n";

    auto diags = diagnostics::extract(engineRes(log, 1));
    ASSERT_EQ(diags.size(), 2u);
    EXPECT_EQ(diags[0].message, "Far line error");
    EXPECT_EQ(diags[0].line, -1);
    EXPECT_EQ(diags[1].message, "Malformed tag error");
    EXPECT_EQ(diags[1].line, -1);
}

TEST(DiagnosticExtractorChar, fatalNoiseKeptWhenAloneAndDroppedOtherwise)
{
    auto alone = diagnostics::extract(engineRes(
        "./main.tex:1: Emergency stop.\n"
        "./main.tex:2: ==> Fatal error occurred.\n",
        1));
    ASSERT_EQ(alone.size(), 2u);
    EXPECT_EQ(alone[0].message, "Emergency stop");
    EXPECT_EQ(alone[1].message, "==> Fatal error occurred");

    auto withReal = diagnostics::extract(engineRes(
        "./main.tex:1: Undefined control sequence.\n"
        "./main.tex:2: Emergency stop.\n"
        "./main.tex:3: ==> Fatal error occurred.\n",
        1));
    ASSERT_EQ(withReal.size(), 1u);
    EXPECT_EQ(withReal[0].message, "Undefined control sequence");
}

TEST_F(DiagnosticExtractorCharFixture, missingFileRelocationAndAuxReattribution)
{
    auto mainPath = writeFile("main.tex",
        "\\documentclass{mycls}\n"
        "% \\usepackage{ghost}\n"
        "\\RequirePackage{alpha, ghost}\n"
        "\\input{chapters/intro}\n"
        "\\include{chapters/body}\n");
    writeFile("main.aux", "");

    auto errDiags = diagnostics::extract(engineRes(
        "! LaTeX Error: File `mycls.cls' not found.\n"
        "l.99 x\n"
        "! LaTeX Error: File `ghost.sty' not found.\n"
        "l.99 y\n"
        "! I can't find file `chapters/intro.tex'.\n"
        "l.99 z\n"
        "! I can't find file `chapters/body'.\n"
        "l.99 w\n",
        1, InvokerKind::PdfLaTeX, mainPath));

    ASSERT_EQ(errDiags.size(), 4u);
    EXPECT_EQ(errDiags[0].line, 1);
    EXPECT_EQ(errDiags[1].line, 3);
    EXPECT_EQ(errDiags[2].line, 4);
    EXPECT_EQ(errDiags[3].line, 5);

    // Warning raised while .aux is open on the file stack is re-attributed to main.tex.
    auto warnDiags = diagnostics::extract(engineRes(
        "(./main.tex (./main.aux\n"
        "LaTeX Warning: Label `sec:a' multiply defined.\n"
        "))\n",
        0, InvokerKind::PdfLaTeX, mainPath));

    ASSERT_EQ(warnDiags.size(), 1u);
    EXPECT_EQ(warnDiags[0].file, "main.tex");
    EXPECT_TRUE(std::filesystem::equivalent(warnDiags[0].sourcePath, mainPath));
    EXPECT_EQ(warnDiags[0].category, "duplicate-label");
    EXPECT_EQ(warnDiags[0].message, "Label `sec:a' multiply defined");
}

// ── LaTeX warnings: every matcher kind ───────────────────────────────────────

TEST(DiagnosticExtractorChar, allLatexWarningKindsAndSummarySkipping)
{
    const std::string log = "LaTeX Warning: Citation `ref1' on page 1 undefined on input line 10.\n"
                            "LaTeX Warning: There were undefined citations.\n"
                            "LaTeX Warning: Label(s) may have changed. Rerun to get cross-references right.\n"
                            "LaTeX Warning: Float too large for page by 5pt\n"
                            "               on input line 22.\n"
                            "   \n"
                            "Package hyperref Warning: Token not allowed\n"
                            "(hyperref)                \n"
                            "(hyperref)                in PDF string.\n"
                            "Class book Warning: Custom class note on input line 3.\n"
                            "LaTeX Font Warning: Font shape `T1/cmr/m/sc' undefined\n"
                            "(Font)              using `T1/cmr/m/n' instead on input line 4.\n"
                            "Missing character: There is no X in font lm!\n"
                            "pdfTeX warning (ext4): destination with the same identifier\n"
                            "Overfull \\hbox (4.0pt too wide) in paragraph at lines 30--31\n"
                            "[]\\T1/cmr/m/n/10 unbalanced ( paren\n"
                            "\n"
                            "Underfull \\vbox (badness 5000) in alignment at lines 40--45\n"
                            "\n";

    auto diags = diagnostics::extract(engineRes(log, 0));
    ASSERT_EQ(diags.size(), 9u);

    EXPECT_EQ(diags[0].category, "undefined-citation");
    EXPECT_EQ(diags[0].line, 10);
    EXPECT_EQ(diags[0].message, "Citation `ref1' on page 1 undefined");

    EXPECT_EQ(diags[1].category, "");
    EXPECT_EQ(diags[1].line, 22);
    EXPECT_EQ(diags[1].message, "Float too large for page by 5pt");

    EXPECT_EQ(diags[2].category, "package");
    EXPECT_EQ(diags[2].line, -1);
    EXPECT_EQ(diags[2].message, "Package hyperref: Token not allowed in PDF string");

    EXPECT_EQ(diags[3].category, "class");
    EXPECT_EQ(diags[3].line, 3);
    EXPECT_EQ(diags[3].message, "Class book: Custom class note");

    EXPECT_EQ(diags[4].category, "font");
    EXPECT_EQ(diags[4].line, 4);
    EXPECT_EQ(diags[4].message, "Font: Font shape `T1/cmr/m/sc' undefined using `T1/cmr/m/n' instead");

    EXPECT_EQ(diags[5].category, "missing-glyph");
    EXPECT_EQ(diags[5].message, "Missing character: There is no X in font lm!");

    EXPECT_EQ(diags[6].category, "pdftex");
    EXPECT_EQ(diags[6].message, "pdfTeX warning (ext4): destination with the same identifier");

    EXPECT_EQ(diags[7].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[7].category, "overfull-hbox");
    EXPECT_EQ(diags[7].line, 30);

    EXPECT_EQ(diags[8].severity, Diagnostic::Severity::Info);
    EXPECT_EQ(diags[8].category, "underfull-vbox");
    EXPECT_EQ(diags[8].line, 40);
}

TEST(DiagnosticExtractorChar, failedEngineWithOnlyWarningDoesNotAddFallback)
{
    auto diags = diagnostics::extract(engineRes(
        "LaTeX Warning: Reference `a' on page 1 undefined on input line 2.\n"
        "Overfull \\hbox (2.0pt too wide) in paragraph at lines 3--4\n\n",
        1));
    ASSERT_EQ(diags.size(), 1u);
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[0].category, "undefined-reference");
}

TEST(DiagnosticExtractorChar, unknownEngineKindFallbackOnlyOnFailure)
{
    EXPECT_TRUE(diagnostics::extract(engineRes("ok\n", 0, InvokerKind::Custom)).empty());
    auto failed = diagnostics::extract(engineRes("boom\n", 1, InvokerKind::Custom));
    ASSERT_EQ(failed.size(), 1u);
    EXPECT_EQ(failed[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(failed[0].rawContext, "boom\n");
}

// ── Tool extractors: bibtex, biber, makeindex, makeglossaries, bib2gls,
//    pythontex, xdvipdfmx, custom, success/failure filtering ──────────────────

TEST(DiagnosticExtractorChar, bibtexSuccessFailureAndFallbackBehaviour)
{
    // Failed bibtex with only warnings prepends fallback at index 0.
    auto warnOnlyFail = diagnostics::extract(toolRes(
        InvokerKind::Bibtex,
        "Warning--empty year in knuth\n",
        2));
    ASSERT_EQ(warnOnlyFail.size(), 2u);
    EXPECT_EQ(warnOnlyFail[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(warnOnlyFail[0].message, "build failed — run with --verbose to see full engine output");
    EXPECT_EQ(warnOnlyFail[1].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(warnOnlyFail[1].message, "empty year in knuth");

    // Successful bibtex strips Error diagnostics, keeping warnings.
    auto okRun = diagnostics::extract(toolRes(
        InvokerKind::Bibtex,
        "I found no database files---while reading file main.aux\n"
        "Warning--I didn't find a database entry for \"k\"\n",
        0));
    ASSERT_EQ(okRun.size(), 1u);
    EXPECT_EQ(okRun[0].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(okRun[0].category, "undefined-citation");

    // Failed bibtex with "I found no database files" alone keeps it.
    auto noDbAlone = diagnostics::extract(toolRes(
        InvokerKind::Bibtex,
        "I found no database files---while reading file main.aux\n",
        2));
    ASSERT_EQ(noDbAlone.size(), 1u);
    EXPECT_EQ(noDbAlone[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(noDbAlone[0].message, "I found no database files---while reading file main.aux");
}

TEST(DiagnosticExtractorChar, allPatternDrivenToolsAndStderrMerge)
{
    auto biber = diagnostics::extract(toolRes(
        InvokerKind::Biber,
        "INFO - start\n"
        "WARN - I didn't find a database entry for 'foo'\n"
        "ERROR - BibTeX subsystem: syntax error\n",
        2));
    ASSERT_EQ(biber.size(), 2u);
    EXPECT_EQ(biber[0].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(biber[0].category, "undefined-citation");
    EXPECT_EQ(biber[1].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(biber[1].message, "BibTeX subsystem: syntax error");

    // Stdout without trailing newline + stderr merge.
    auto makeindex = diagnostics::extract(toolRes(
        InvokerKind::Makeindex,
        "!! Input index error",
        1,
        "## Warning (input = main.idx, line = 3)\n"));
    ASSERT_EQ(makeindex.size(), 2u);
    EXPECT_EQ(makeindex[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(makeindex[0].message, "Input index error");
    EXPECT_EQ(makeindex[1].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(makeindex[1].message, "Warning (input = main.idx, line = 3)");

    auto makeglossaries = diagnostics::extract(toolRes(
        InvokerKind::Makeglossaries,
        "Entry text mentioning Error without colon\n"
        "!! idx err\n"
        "## idx warn\n"
        "*** unable to execute\n"
        "makeglossaries: Error: bad option\n"
        "makeglossaries: Warning: empty glossary\n",
        1));
    ASSERT_EQ(makeglossaries.size(), 5u);
    EXPECT_EQ(makeglossaries[0].message, "idx err");
    EXPECT_EQ(makeglossaries[1].message, "idx warn");
    EXPECT_EQ(makeglossaries[2].message, "unable to execute");
    EXPECT_EQ(makeglossaries[3].message, "makeglossaries: Error: bad option");
    EXPECT_EQ(makeglossaries[4].message, "makeglossaries: Warning: empty glossary");

    auto bib2gls = diagnostics::extract(toolRes(
        InvokerKind::Bib2gls,
        "Error: one\nERROR: two\nWarning: three\nWARNING: four\n",
        1));
    ASSERT_EQ(bib2gls.size(), 4u);
    EXPECT_EQ(bib2gls[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(bib2gls[1].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(bib2gls[2].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(bib2gls[3].severity, Diagnostic::Severity::Warning);

    auto pythontex = diagnostics::extract(toolRes(
        InvokerKind::Pythontex,
        "PythonTeX error: NameError\nPythonTeX warning: deprecated\n",
        1));
    ASSERT_EQ(pythontex.size(), 2u);
    EXPECT_EQ(pythontex[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(pythontex[1].severity, Diagnostic::Severity::Warning);

    auto xdv = diagnostics::extract(toolRes(
        InvokerKind::Xdvipdfmx,
        "xdvipdfmx:fatal: cannot open file\n"
        "xdvipdfmx:warning: missing glyph\n"
        "** ERROR ** bad font\n"
        "** WARNING ** odd metric\n",
        1));
    ASSERT_EQ(xdv.size(), 4u);
    EXPECT_EQ(xdv[0].message, " cannot open file");
    EXPECT_EQ(xdv[1].message, " missing glyph");
    EXPECT_EQ(xdv[2].message, " bad font");
    EXPECT_EQ(xdv[3].message, " odd metric");

    auto custom = diagnostics::extract(toolRes(
        InvokerKind::Custom,
        "ERROR: e1\nError: e2\nerror: e3\n!! e4\n"
        "WARNING: w1\nWarning: w2\nwarning: w3\n## w4\n",
        1));
    ASSERT_EQ(custom.size(), 8u);
    for (size_t i = 0; i < 4; ++i)
        EXPECT_EQ(custom[i].severity, Diagnostic::Severity::Error);
    for (size_t i = 4; i < 8; ++i)
        EXPECT_EQ(custom[i].severity, Diagnostic::Severity::Warning);
}

// ── Abnormal runs (notFound, timedOut, cancelled, precedence) ────────────────

TEST(DiagnosticExtractorChar, abnormalRunsForEngineAndTool)
{
    RunResult<EngineInvocation> engNotFound {
        .invocation = EngineInvocation { .texFile = "main.tex" },
        .kind = InvokerKind::PdfLaTeX,
        .exitCode = 127,
        .program = "pdflatex",
        .notFound = true,
        .timedOut = true,
        .cancelled = true,
    };
    auto d1 = diagnostics::extract(engNotFound);
    ASSERT_EQ(d1.size(), 1u);
    EXPECT_EQ(d1[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(d1[0].category, "missing-tool");
    EXPECT_EQ(d1[0].missingFile, "pdflatex");
    EXPECT_EQ(d1[0].message, "pdflatex is not installed (not found on PATH)");

    RunResult<EngineInvocation> engTimeout {
        .invocation = EngineInvocation { .texFile = "main.tex" },
        .kind = InvokerKind::PdfLaTeX,
        .exitCode = -1,
        .program = "pdflatex",
        .timedOut = true,
        .cancelled = true,
    };
    auto d2 = diagnostics::extract(engTimeout);
    ASSERT_EQ(d2.size(), 1u);
    EXPECT_EQ(d2[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(d2[0].category, "timeout");
    EXPECT_EQ(d2[0].message, "pdflatex was stopped: it ran longer than the timeout");

    RunResult<ToolInvocation> toolCancelled {
        .invocation = ToolInvocation { .jobName = "main" },
        .kind = InvokerKind::Biber,
        .exitCode = -1,
        .program = "biber",
        .cancelled = true,
    };
    auto d3 = diagnostics::extract(toolCancelled);
    ASSERT_EQ(d3.size(), 1u);
    EXPECT_EQ(d3[0].severity, Diagnostic::Severity::Info);
    EXPECT_EQ(d3[0].category, "cancelled");
    EXPECT_EQ(d3[0].message, "build cancelled");
}
