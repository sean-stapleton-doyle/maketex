#include "maketex/diagnostics/LatexWarnings.h"

#include "FakeSourceResolver.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace maketex;
using namespace maketex::diagnostics;
using maketex::test_utils::FakeSourceResolver;
namespace fs = std::filesystem;

TEST(DiagnosticsLatexWarnings, matchersHandleOutOfBoundsAndNonMatchingCursor)
{
    const std::vector<std::string> lines { "plain log text" };
    constexpr WarningMatcher kMatchers[] = {
        matchLatexWarning,
        matchNamedWarning,
        matchMissingCharacter,
        matchPdfTexWarning,
        matchBoxWarning,
    };

    for (WarningMatcher m : kMatchers) {
        LineCursor nonMatch { lines, 0 };
        EXPECT_FALSE(m(nonMatch).matched);
        EXPECT_EQ(nonMatch.index, 0u);

        LineCursor eof { lines, lines.size() };
        EXPECT_FALSE(m(eof).matched);
        EXPECT_EQ(eof.index, lines.size());
    }
}

TEST(DiagnosticsLatexWarnings, matchLatexWarningContinuationsAndSummaries)
{
    const std::vector<std::string> lines {
        "LaTeX Warning: Reference `sec:a' on page 1",
        "               undefined on input line 14.",
        "    ", // whitespace-only stops continuation
        "LaTeX Warning: There were undefined references.",
        "LaTeX Warning: Label(s) may have changed.",
        "               Rerun to get cross-references right.",
    };

    LineCursor c { lines, 0 };
    auto m1 = matchLatexWarning(c);
    ASSERT_TRUE(m1.matched);
    ASSERT_TRUE(m1.diagnostic.has_value());
    EXPECT_EQ(c.index, 2u);
    EXPECT_EQ(m1.diagnostic->line, 14);
    EXPECT_EQ(m1.diagnostic->category, "undefined-reference");
    EXPECT_EQ(m1.diagnostic->message, "Reference `sec:a' on page 1 undefined");

    // Advance past whitespace line to first summary line.
    c.index = 3;
    auto m2 = matchLatexWarning(c);
    EXPECT_TRUE(m2.matched);
    EXPECT_FALSE(m2.diagnostic.has_value());
    EXPECT_EQ(c.index, 4u);

    auto m3 = matchLatexWarning(c);
    EXPECT_TRUE(m3.matched);
    EXPECT_FALSE(m3.diagnostic.has_value());
    EXPECT_EQ(c.index, 6u);
}

TEST(DiagnosticsLatexWarnings, matchNamedWarningPackageClassFontAndCategoryOverride)
{
    const std::vector<std::string> lines {
        "Package natbib Warning: Citation `a' on page 2",
        "(natbib)                ",
        "(natbib)                undefined on input line 8.",
        "Class memoir Warning: Option deprecated.",
        "LaTeX Font Warning: Font shape `T1/lmr/m/sc' undefined",
        "(Font)              using `T1/lmr/m/n' instead on input line 5.",
    };

    LineCursor c { lines, 0 };
    auto m1 = matchNamedWarning(c);
    ASSERT_TRUE(m1.matched);
    ASSERT_TRUE(m1.diagnostic.has_value());
    EXPECT_EQ(c.index, 3u);
    EXPECT_EQ(m1.diagnostic->category, "undefined-citation");
    EXPECT_EQ(m1.diagnostic->line, 8);
    EXPECT_EQ(m1.diagnostic->message, "Package natbib: Citation `a' on page 2 undefined");

    auto m2 = matchNamedWarning(c);
    ASSERT_TRUE(m2.matched);
    ASSERT_TRUE(m2.diagnostic.has_value());
    EXPECT_EQ(c.index, 4u);
    EXPECT_EQ(m2.diagnostic->category, "class");
    EXPECT_EQ(m2.diagnostic->message, "Class memoir: Option deprecated");

    auto m3 = matchNamedWarning(c);
    ASSERT_TRUE(m3.matched);
    ASSERT_TRUE(m3.diagnostic.has_value());
    EXPECT_EQ(c.index, 6u);
    EXPECT_EQ(m3.diagnostic->category, "font");
    EXPECT_EQ(m3.diagnostic->line, 5);
    EXPECT_EQ(m3.diagnostic->message,
        "Font: Font shape `T1/lmr/m/sc' undefined using `T1/lmr/m/n' instead");
}

TEST(DiagnosticsLatexWarnings, matchMissingCharacterPdfTexAndBoxContentSkipping)
{
    const std::vector<std::string> lines {
        "Missing character: There is no Z in font lm!",
        "pdfTeX warning (ext4): duplicate dest",
        "Overfull \\hbox (8.0pt too wide) in paragraph at lines 10--12",
        "[]\\T1/cmr/m/n/10 content with unbalanced ( paren",
        "second content line )",
        "",
        "Underfull \\vbox (badness 10000) detected at line 20",
        "content running to EOF without blank line",
    };

    LineCursor c { lines, 0 };
    auto m1 = matchMissingCharacter(c);
    ASSERT_TRUE(m1.matched && m1.diagnostic.has_value());
    EXPECT_EQ(m1.diagnostic->category, "missing-glyph");
    EXPECT_EQ(c.index, 1u);

    auto m2 = matchPdfTexWarning(c);
    ASSERT_TRUE(m2.matched && m2.diagnostic.has_value());
    EXPECT_EQ(m2.diagnostic->category, "pdftex");
    EXPECT_EQ(c.index, 2u);

    auto m3 = matchBoxWarning(c);
    ASSERT_TRUE(m3.matched && m3.diagnostic.has_value());
    EXPECT_EQ(m3.diagnostic->severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(m3.diagnostic->category, "overfull-hbox");
    EXPECT_EQ(m3.diagnostic->line, 10);
    EXPECT_EQ(c.index, 5u); // stops at the blank line

    c.index = 6;
    auto m4 = matchBoxWarning(c);
    ASSERT_TRUE(m4.matched && m4.diagnostic.has_value());
    EXPECT_EQ(m4.diagnostic->severity, Diagnostic::Severity::Info);
    EXPECT_EQ(m4.diagnostic->category, "underfull-vbox");
    EXPECT_EQ(m4.diagnostic->line, 20);
    EXPECT_EQ(c.index, 8u); // consumes to EOF
}

TEST(DiagnosticsLatexWarnings, auxReattributionBoxSkippingAndSourceEchoWithFakeResolver)
{
    FakeSourceResolver resolver;
    resolver.addFile("/project/main.tex");
    resolver.addFile("/project/chapters/ch1.tex");
    resolver.addFile("/project/main.aux");

    const std::string log = "(./main.tex (./chapters/ch1.tex\n"
                            "Overfull \\hbox (6.0pt too wide) in paragraph at lines 3--4\n"
                            "[]\\T1/cmr/m/n/10 stray closing ))))\n"
                            "\n"
                            "l.5 echoed source with stray ))))\n"
                            "    continued echo )))\n"
                            "LaTeX Warning: Reference `fig:1' on page 1 undefined on input line 9.\n"
                            ") (./main.aux\n"
                            "LaTeX Warning: Label `sec:dup' multiply defined.\n"
                            "))\n";

    auto diags = extractLatexWarnings(log, "/project/main.tex", resolver);
    ASSERT_EQ(diags.size(), 3u);

    EXPECT_EQ(diags[0].category, "overfull-hbox");
    EXPECT_EQ(diags[0].file, "chapters/ch1.tex");
    EXPECT_EQ(diags[0].sourcePath, fs::path("/project/chapters/ch1.tex"));

    // Neither box content nor source-echo lines popped chapters/ch1.tex.
    EXPECT_EQ(diags[1].category, "undefined-reference");
    EXPECT_EQ(diags[1].file, "chapters/ch1.tex");
    EXPECT_EQ(diags[1].sourcePath, fs::path("/project/chapters/ch1.tex"));
    EXPECT_EQ(diags[1].line, 9);

    // Warning raised while main.aux is open is re-attributed to root main.tex.
    EXPECT_EQ(diags[2].category, "duplicate-label");
    EXPECT_EQ(diags[2].file, "main.tex");
    EXPECT_EQ(diags[2].sourcePath, fs::path("/project/main.tex"));
}
