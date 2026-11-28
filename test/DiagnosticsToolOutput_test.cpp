#include "maketex/DiagnosticExtractor.h"
#include "maketex/diagnostics/ToolOutput.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace maketex;
using namespace maketex::diagnostics;

TEST(DiagnosticsToolOutput, extractByLinePatternsPrefixContainsAndPrecedence)
{
    const std::vector<LinePattern> patterns {
        { "!! ", true, Diagnostic::Severity::Error, true },
        { "ERR:", false, Diagnostic::Severity::Error, false },
        { "WARN:", false, Diagnostic::Severity::Warning, true },
    };

    const std::string output = "normal line\n"
                               "  !! not at start so ignored\n"
                               "!! prefix error with WARN: inside\n"
                               "prefix ERR: whole line kept\n"
                               "prefix WARN: trimmed\n";

    auto diags = extractByLinePatterns(output, patterns);
    ASSERT_EQ(diags.size(), 3u);

    // First matching pattern wins even though WARN: also appears on the line.
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[0].message, "prefix error with WARN: inside");

    EXPECT_EQ(diags[1].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[1].message, "prefix ERR: whole line kept");

    EXPECT_EQ(diags[2].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[2].message, " trimmed");
}

TEST(DiagnosticsToolOutput, extractBibtexSameLineNextLineAndChatter)
{
    const std::string output = "This is BibTeX, Version 0.99e\n"
                               "The top-level auxiliary file: main.aux\n"
                               "The style file: plain.bst\n"
                               "Database file #1: refs.bib\n"
                               "---orphan separator line\n"
                               "I was expecting a `,'---line 12 of file refs.bib\n"
                               " : @article{a\n"
                               " :           author={X}}\n"
                               "I'm skipping whatever remains of this entry\n"
                               "Repeated entry\n"
                               "---line 44 of file refs.bib\n"
                               " : @article{a,\n"
                               "Unlocated bibtex error\n"
                               "Warning--empty journal in a\n"
                               "Warning--I didn't find a database entry for \"ghost\"\n"
                               "(There were 3 error messages)\n";

    auto diags = extractBibtex(output);
    ASSERT_EQ(diags.size(), 5u);

    // Same-line location.
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[0].message, "I was expecting a `,'");
    EXPECT_EQ(diags[0].file, "refs.bib");
    EXPECT_EQ(diags[0].line, 12);
    EXPECT_NE(diags[0].rawContext.find("@article{a"), std::string::npos);

    // Next-line location.
    EXPECT_EQ(diags[1].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[1].message, "Repeated entry");
    EXPECT_EQ(diags[1].file, "refs.bib");
    EXPECT_EQ(diags[1].line, 44);

    // No location.
    EXPECT_EQ(diags[2].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[2].message, "Unlocated bibtex error");
    EXPECT_TRUE(diags[2].file.empty());
    EXPECT_EQ(diags[2].line, -1);

    // Warnings.
    EXPECT_EQ(diags[3].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[3].category, "");
    EXPECT_EQ(diags[3].message, "empty journal in a");

    EXPECT_EQ(diags[4].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[4].category, "undefined-citation");
}

TEST(DiagnosticsToolOutput, extractToolOutputTableCoversEveryTool)
{
    struct Case {
        InvokerKind kind;
        std::string output;
        std::size_t expectedCount;
        Diagnostic::Severity firstSeverity;
        std::string firstMessage;
        std::string firstCategory;
    };

    const std::vector<Case> cases {
        { InvokerKind::Bibtex, "Warning--I didn't find a database entry for \"k\"\n",
            1, Diagnostic::Severity::Warning, "I didn't find a database entry for \"k\"",
            "undefined-citation" },
        { InvokerKind::Biber, "WARN - I didn't find a database entry for 'k'\n",
            1, Diagnostic::Severity::Warning, "I didn't find a database entry for 'k'",
            "undefined-citation" },
        { InvokerKind::Makeindex, "!! bad key\n## skipped entry\n",
            2, Diagnostic::Severity::Error, "bad key", "" },
        { InvokerKind::Makeglossaries, "*** failed to run xindy\n",
            1, Diagnostic::Severity::Error, "failed to run xindy", "" },
        { InvokerKind::Bib2gls, "ERROR: missing bib\nWARNING: unused\n",
            2, Diagnostic::Severity::Error, "ERROR: missing bib", "" },
        { InvokerKind::Pythontex, "PythonTeX error: ZeroDivisionError\n",
            1, Diagnostic::Severity::Error, "PythonTeX error: ZeroDivisionError", "" },
        { InvokerKind::Xdvipdfmx, "** ERROR ** invalid xdv\n",
            1, Diagnostic::Severity::Error, " invalid xdv", "" },
        { InvokerKind::Custom, "error: bad flag\n## notice\n",
            2, Diagnostic::Severity::Error, "bad flag", "" },
    };

    for (const auto& c : cases) {
        auto diags = extractToolOutput(c.kind, c.output);
        ASSERT_EQ(diags.size(), c.expectedCount);
        EXPECT_EQ(diags[0].severity, c.firstSeverity);
        EXPECT_EQ(diags[0].message, c.firstMessage);
        EXPECT_EQ(diags[0].category, c.firstCategory);
    }

    EXPECT_TRUE(extractToolOutput(InvokerKind::PdfLaTeX, "ERROR: ignored\n").empty());
}

TEST(DiagnosticsToolOutput, toolDispatchMergesStreamsAndAppliesFallbackPolicy)
{
    // Empty stdout + non-empty stderr on failure.
    RunResult<ToolInvocation> stderrOnly {
        .invocation = ToolInvocation { .jobName = "main" },
        .kind = InvokerKind::Makeindex,
        .exitCode = 1,
        .stdoutOutput = "",
        .stderrOutput = "!! failed on stderr\n",
    };
    auto d1 = diagnostics::extract(stderrOnly);
    ASSERT_EQ(d1.size(), 1u);
    EXPECT_EQ(d1[0].message, "failed on stderr");

    // Stdout already ends with newline + stderr merged cleanly.
    RunResult<ToolInvocation> withNewline {
        .invocation = ToolInvocation { .jobName = "main" },
        .kind = InvokerKind::Makeindex,
        .exitCode = 0,
        .stdoutOutput = "!! ignored on success\n",
        .stderrOutput = "## kept warning\n",
    };
    auto d2 = diagnostics::extract(withNewline);
    ASSERT_EQ(d2.size(), 1u);
    EXPECT_EQ(d2[0].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(d2[0].message, "kept warning");

    // Failed run with no structured errors gets fallback Error.
    RunResult<ToolInvocation> unparsedFail {
        .invocation = ToolInvocation { .jobName = "main" },
        .kind = InvokerKind::Custom,
        .exitCode = 1,
        .stdoutOutput = "mysterious crash\n",
    };
    auto d3 = diagnostics::extract(unparsedFail);
    ASSERT_EQ(d3.size(), 1u);
    EXPECT_EQ(d3[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(d3[0].rawContext, "mysterious crash\n");
}
