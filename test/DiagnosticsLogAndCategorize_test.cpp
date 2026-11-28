#include "maketex/diagnostics/Categorize.h"
#include "maketex/diagnostics/LogLines.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace maketex::diagnostics;

TEST(DiagnosticsLogLines, splitLinesMatchesGetlineSemantics)
{
    struct Case {
        std::string_view input;
        std::vector<std::string> expected;
    };
    const std::vector<Case> cases {
        { "", { } },
        { "single", { "single" } },
        { "single\n", { "single" } },
        { "\n", { "" } },
        { "\n\n", { "", "" } },
        { "a\nb\nc", { "a", "b", "c" } },
        { "a\nb\n\n", { "a", "b", "" } },
    };

    for (const auto& c : cases)
        EXPECT_EQ(splitLines(c.input), c.expected) << "input=[" << c.input << "]";
}

TEST(DiagnosticsLogLines, tryParseIntCases)
{
    struct Case {
        std::string_view input;
        std::optional<int> expected;
    };
    const std::vector<Case> cases {
        { "0", 0 },
        { "42", 42 },
        { "-7", -7 },
        { "", std::nullopt },
        { "abc", std::nullopt },
        { "12abc", std::nullopt },
        { " 12", std::nullopt },
        { "99999999999999999999", std::nullopt },
    };

    for (const auto& c : cases)
        EXPECT_EQ(tryParseInt(c.input), c.expected) << "input=[" << c.input << "]";
}

TEST(DiagnosticsLogLines, quotedNameCases)
{
    struct Case {
        std::string_view input;
        std::string expected;
    };
    const std::vector<Case> cases {
        { "File `minted.sty' not found", "minted.sty" },
        { "I can't find file `chapters/intro'", "chapters/intro" },
        { "No quotes here", "" },
        { "Only open `quote", "" },
        { "Only close quote'", "" },
        { "Close ' before `open", "" },
        { "Empty `' name", "" },
    };

    for (const auto& c : cases)
        EXPECT_EQ(quotedName(c.input), c.expected) << "input=[" << c.input << "]";
}

TEST(DiagnosticsCategorize, categorizeErrorTable)
{
    struct Case {
        std::string_view msg;
        std::string expected;
    };
    const std::vector<Case> cases {
        { "Undefined control sequence", "undefined-macro" },
        { "Package fontspec Error: The font \"X\" cannot be found", "missing-font" },
        { "Font \\a=b not loadable: Metric (TFM) file not found", "missing-font" },
        { "This font cannot be loaded right now", "missing-font" },
        { "Package fontspec Error: unrelated font note", "package" },
        { "File `minted.sty' not found", "missing-file" },
        { "File `x.sty' has wrong version", "" },
        { "I can't find file `chapter'", "missing-file" },
        { "Package minted Error: invoke with -shell-escape", "shell-escape-required" },
        { "Enable shell escape to run external tool", "shell-escape-required" },
        { "\\write18 is disabled", "shell-escape-required" },
        { "Package tikz Error: bad coordinate", "package" },
        { "Missing $ inserted", "" },
    };

    for (const auto& c : cases)
        EXPECT_EQ(categorizeError(c.msg), c.expected) << "msg=[" << c.msg << "]";
}

TEST(DiagnosticsCategorize, categorizeWarningTable)
{
    struct Case {
        std::string_view msg;
        std::string expected;
    };
    const std::vector<Case> cases {
        { "Citation `knuth' on page 1 undefined", "undefined-citation" },
        { "Citation `knuth' looks fine", "" },
        { "Reference `fig:1' on page 2 undefined", "undefined-reference" },
        { "Reference `fig:1' defined", "" },
        { "Label `sec:a' multiply defined", "duplicate-label" },
        { "Float too large for page", "" },
    };

    for (const auto& c : cases)
        EXPECT_EQ(categorizeWarning(c.msg), c.expected) << "msg=[" << c.msg << "]";
}

TEST(DiagnosticsCategorize, cleanErrorMessageAndInputLineHelpers)
{
    EXPECT_EQ(cleanErrorMessage("LaTeX Error: Bad math."), "Bad math");
    EXPECT_EQ(cleanErrorMessage("LaTeX Error: No trailing dot"), "No trailing dot");
    EXPECT_EQ(cleanErrorMessage("Plain error."), "Plain error");
    EXPECT_EQ(cleanErrorMessage(""), "");

    EXPECT_EQ(extractInputLine("Ref undefined on input line 42."), 42);
    EXPECT_EQ(extractInputLine("first on input line 3 then on input line 17."), 17);
    EXPECT_EQ(extractInputLine("no line tag here"), -1);
    EXPECT_EQ(extractInputLine("on input line abc"), -1);
    EXPECT_EQ(extractInputLine("on input line 99999999999999999999"), -1);

    std::string s1 = "Warning text on input line 9.";
    stripInputLineSuffix(s1);
    EXPECT_EQ(s1, "Warning text");

    std::string s2 = "Bare sentence.";
    stripInputLineSuffix(s2);
    EXPECT_EQ(s2, "Bare sentence");

    std::string s3 = "No dot";
    stripInputLineSuffix(s3);
    EXPECT_EQ(s3, "No dot");

    std::string s4;
    stripInputLineSuffix(s4);
    EXPECT_EQ(s4, "");
}
