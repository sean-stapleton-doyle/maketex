#include "maketex/DiagnosticExtractor.h"
#include "maketex/diagnostics/LatexErrors.h"

#include "FakeSourceResolver.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace maketex;
using namespace maketex::diagnostics;
using maketex::test_utils::FakeSourceResolver;
namespace fs = std::filesystem;

TEST(DiagnosticsLatexErrors, parseFileLineErrorValidAndProseFalsePositives)
{
    FakeSourceResolver resolver;
    resolver.addFile("/project/my file.tex");
    resolver.addFile("/project/custom.dat");

    const fs::path workDir = "/project";

    // Valid TeX extension even when absent on disk.
    auto m1 = parseFileLineError("./chap.tex:12: Undefined control sequence.", workDir, resolver);
    ASSERT_TRUE(m1.has_value());
    EXPECT_EQ(m1->fileToken, "./chap.tex");
    EXPECT_EQ(m1->line, 12);
    EXPECT_EQ(m1->message, "Undefined control sequence.");

    // Existing file with spaces (quoted) and existing non-TeX extension.
    auto m2 = parseFileLineError("\"./my file.tex\":7: Bad macro.", workDir, resolver);
    ASSERT_TRUE(m2.has_value());
    EXPECT_EQ(m2->fileToken, "./my file.tex");
    EXPECT_EQ(m2->line, 7);

    auto m3 = parseFileLineError("custom.dat:4: Unexpected token.", workDir, resolver);
    ASSERT_TRUE(m3.has_value());
    EXPECT_EQ(m3->fileToken, "custom.dat");

    // False positives and malformed markers must return nullopt.
    for (const char* nonError : {
             "Error at absent.tex:3: prose with spaces",
             "some note: 12: space before digits",
             "unknown.xyz:12: non-tex extension not on disk",
             ":12: empty file token",
             "\"\":12: empty quoted token",
             "./main.tex:12:no-space-after-colon",
             "./main.tex:abc: non-numeric line",
             "./main.tex:99999999999999999999: integer overflow",
             "./main.tex:5:  ==> Fatal error occurred",
             "no colons at all",
         }) {
        EXPECT_FALSE(parseFileLineError(nonError, workDir, resolver).has_value())
            << "line=[" << nonError << "]";
    }
}

TEST(DiagnosticsLatexErrors, gatherContextLookAheadCollisionAndBoundaries)
{
    FakeSourceResolver resolver;
    const fs::path workDir = "/project";

    // Look-ahead collides with next `! ` and next `file:line:` errors.
    const std::vector<std::string> lines {
        "b0",
        "b1",
        "b2",
        "b3",
        "b4",
        "b5",
        "b6",
        "b7", // 8 lines before index 8
        "! First error.",
        "detail 1",
        "! Second error.",
        "l.25", // no trailing space after line number
        "./ch.tex:30: Third error.",
        "detail 2",
        "./ch.tex:40: Fourth error.",
        "l.99999999999999999999 overflow",
    };

    auto c1 = gatherContext(lines, 8, workDir, resolver);
    EXPECT_EQ(c1.line, -1);
    EXPECT_EQ(c1.rawCtx.find("b0\n"), std::string::npos); // capped at 7 before
    EXPECT_NE(c1.rawCtx.find("b1\n"), std::string::npos);
    EXPECT_NE(c1.rawCtx.find("detail 1\n"), std::string::npos);
    EXPECT_EQ(c1.rawCtx.find("Second error"), std::string::npos);

    auto c2 = gatherContext(lines, 10, workDir, resolver);
    EXPECT_EQ(c2.line, 25);

    auto c3 = gatherContext(lines, 12, workDir, resolver);
    EXPECT_EQ(c3.line, -1);
    EXPECT_EQ(c3.rawCtx.find("Fourth error"), std::string::npos);

    auto c4 = gatherContext(lines, 14, workDir, resolver);
    EXPECT_EQ(c4.line, -1);

    auto oob = gatherContext(lines, lines.size(), workDir, resolver);
    EXPECT_TRUE(oob.rawCtx.empty());
    EXPECT_EQ(oob.line, -1);
}

TEST(DiagnosticsLatexErrors, foldContinuationCases)
{
    const std::vector<std::string> lines {
        "header",
        "(fontspec)   First line",
        "(fontspec)   ",
        "(fontspec)   second line.",
        "Not a continuation",
        "(fontspec)   unreachable",
    };

    EXPECT_EQ(foldContinuation("Package fontspec Error: ", lines, 1),
        "Package fontspec Error: First line second line.");
    EXPECT_EQ(foldContinuation("No trailing space", lines, 1),
        "No trailing space First line second line.");
    EXPECT_EQ(foldContinuation("", lines, 1), "First line second line.");
    EXPECT_EQ(foldContinuation("Unchanged", lines, 4), "Unchanged");
    EXPECT_EQ(foldContinuation("At end", lines, lines.size()), "At end");
}

TEST(DiagnosticsLatexErrors, loadingLineTable)
{
    const std::string doc = "\\documentclass[12pt]{myclass}\n"
                            "% \\usepackage{commented}\n"
                            "\\usepackage{amsmath, biblatex , csquotes}\n"
                            "\\RequirePackage[utf8]{inputenc, fontenc}\n"
                            "\\usepackage no_brace\n"
                            "\\input{chapters/intro}\n"
                            "\\include{chapters/results.tex}\n"
                            "\\usepackage{unclosed\n";

    struct Case {
        const char* missing;
        int expectedLine;
    };
    const std::vector<Case> cases {
        { "myclass.cls", 1 },
        { "commented.sty", -1 },
        { "amsmath.sty", 3 },
        { "biblatex.sty", 3 },
        { "csquotes.sty", 3 },
        { "bib.sty", -1 }, // partial stem must not match biblatex
        { "fontenc.sty", 4 },
        { "chapters/intro.tex", 6 },
        { "chapters/intro", 6 },
        { "chapters/results.tex", 7 },
        { "unclosed.sty", 8 },
        { "absent.sty", -1 },
    };

    for (const auto& c : cases)
        EXPECT_EQ(loadingLine(doc, c.missing), c.expectedLine) << c.missing;
}

TEST(DiagnosticsLatexErrors, refineErrorsRelocationAndFallbackWithFakeResolver)
{
    FakeSourceResolver resolver;
    resolver.addFile("/project/main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{foo, missingpkg}\n");
    resolver.addFile("/project/chapters/ch1.tex",
        "\\input{chapters/sub}\n");
    resolver.addUnreadableFile("/project/unreadable.tex");

    // Empty sourcePath falls back to texFile and populates sourcePath + file.
    std::vector<Diagnostic> d1 {
        Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .line = 99,
            .message = "File `missingpkg.sty' not found",
            .category = "missing-file",
        },
        Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .message = "Emergency stop",
        },
    };
    refineErrors(d1, "/project/main.tex", resolver);
    ASSERT_EQ(d1.size(), 1u);
    EXPECT_EQ(d1[0].line, 2);
    EXPECT_EQ(d1[0].file, "main.tex");
    EXPECT_EQ(d1[0].sourcePath, fs::path("/project/main.tex"));
    EXPECT_EQ(d1[0].missingFile, "missingpkg.sty");

    // Existing sourcePath searches that child file, not main.tex.
    std::vector<Diagnostic> d2 {
        Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .file = "chapters/ch1.tex",
            .sourcePath = "/project/chapters/ch1.tex",
            .line = 50,
            .message = "I can't find file `chapters/sub.tex'",
            .category = "missing-file",
        },
    };
    refineErrors(d2, "/project/main.tex", resolver);
    EXPECT_EQ(d2[0].line, 1);

    // Unreadable file and unquoted missing-file message leave line unchanged.
    std::vector<Diagnostic> d3 {
        Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .sourcePath = "/project/unreadable.tex",
            .line = 12,
            .message = "File `missingpkg.sty' not found",
            .category = "missing-file",
        },
        Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .line = 13,
            .message = "I can't find file without quotes",
            .category = "missing-file",
        },
    };
    refineErrors(d3, "/project/main.tex", resolver);
    EXPECT_EQ(d3[0].line, 12);
    EXPECT_EQ(d3[1].line, 13);
}

TEST(DiagnosticsLatexErrors, extractOverloadUsesInjectedSourceResolver)
{
    FakeSourceResolver resolver;
    resolver.addFile("/project/main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{alpha, nope}\n");
    resolver.addFile("/project/my chapter.tex", "\\bad\n");

    RunResult<EngineInvocation> res {
        .invocation = EngineInvocation { .texFile = "/project/main.tex" },
        .kind = InvokerKind::PdfLaTeX,
        .exitCode = 1,
        .stdoutOutput = "(./main.tex (\"./my chapter.tex\"\n"
                        "! Undefined control sequence.\n"
                        "l.1 see (paren)\n"
                        "               \\bad\n"
                        "! LaTeX Error: File `nope.sty' not found.\n"
                        "l.2 \\foo\n",
    };

    auto diags = diagnostics::extract(res, resolver);
    ASSERT_EQ(diags.size(), 2u);
    EXPECT_EQ(diags[0].file, "my chapter.tex");
    EXPECT_EQ(diags[0].sourcePath, fs::path("/project/my chapter.tex"));
    EXPECT_EQ(diags[0].line, 1);
    EXPECT_EQ(diags[1].file, "my chapter.tex");
    EXPECT_EQ(diags[1].missingFile, "nope.sty");
    EXPECT_EQ(diags[1].line, 2);
}
