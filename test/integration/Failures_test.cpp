// The testdata/failures/ fixtures, built with the real engines: each must
// fail (or warn) the documented way, with its first diagnostic pointing at
// the right file and line. These pin the extractor against what TeX Live
// actually prints, not against hand-written log excerpts.

#include "FixtureHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

using namespace maketex;
using namespace maketex::integration;

namespace {

struct FailureCase {
    std::string name; // under testdata/failures/
    EngineKind engine = EngineKind::PdfLaTeX;
    std::vector<std::string> programs;
    std::vector<std::string> texFiles;
    bool succeeds = false;
    // The first diagnostic of the expected severity.
    Diagnostic::Severity severity = Diagnostic::Severity::Error;
    std::string category; // empty: untagged
    std::string file;
    int line = -1;
    std::string messageContains;

    friend void PrintTo(const FailureCase& c, std::ostream* os) { *os << c.name; }
};

const std::vector<FailureCase> kFailures = {
    { .name = "err-undefined-command",
        .category = "undefined-macro",
        .file = "main.tex",
        .line = 27,
        .messageContains = "Undefined control sequence" },
    { .name = "err-missing-package",
        .category = "missing-file",
        .file = "main.tex",
        .line = 23,
        .messageContains = "no-such-package-maketex-test.sty" },
    { .name = "err-missing-input",
        .category = "missing-file",
        .file = "main.tex",
        .line = 29,
        .messageContains = "this-file-does-not-exist.tex" },
    { .name = "err-runaway-argument",
        .file = "main.tex",
        .messageContains = "File ended while scanning use of \\textbf" },
    { .name = "err-unclosed-environment",
        .file = "main.tex",
        .line = 30,
        .messageContains = "\\begin{center} on input line 26 ended by \\end{document}" },
    { .name = "err-nested-input",
        .category = "undefined-macro",
        .file = "chapters/ch1.tex",
        .line = 9 },
    { .name = "err-bibtex-missing-bib",
        .programs = { "bibtex" },
        .category = "missing-file",
        .file = "no-such-bib-file.bib",
        .messageContains = "I couldn't open database file" },
    { .name = "err-bibtex-bad-entry",
        .programs = { "bibtex" },
        .file = "bad.bib",
        .line = 11 },
    { .name = "err-missing-font",
        .engine = EngineKind::XeLaTeX,
        .texFiles = { "fontspec.sty" },
        .category = "missing-font",
        .file = "main.tex",
        .line = 22,
        .messageContains = "No Such Font 2026" },
    { .name = "warn-undefined-refs",
        .succeeds = true,
        .severity = Diagnostic::Severity::Warning,
        .category = "undefined-reference",
        .file = "main.tex",
        .line = 29,
        .messageContains = "no-such-label" },
    { .name = "warn-duplicate-label",
        .succeeds = true,
        .severity = Diagnostic::Severity::Warning,
        .category = "duplicate-label",
        .file = "main.tex",
        .messageContains = "sec:duplicate" },
    { .name = "warn-overfull-hbox",
        .succeeds = true,
        .severity = Diagnostic::Severity::Warning,
        .category = "overfull-hbox",
        .file = "main.tex",
        .line = 32 },
    { .name = "warn-undefined-citation",
        .succeeds = true,
        .severity = Diagnostic::Severity::Warning,
        .category = "undefined-citation",
        .file = "main.tex",
        .line = 22,
        .messageContains = "nosuchkey2026" },
    { .name = "warn-package-warning",
        .texFiles = { "hyperref.sty" },
        .succeeds = true,
        .severity = Diagnostic::Severity::Warning,
        .category = "package",
        .file = "main.tex",
        .line = 24,
        .messageContains = "Package hyperref: Token not allowed" },
};

class FailureFixtureTest : public ::testing::TestWithParam<FailureCase> { };

TEST_P(FailureFixtureTest, reportsTheDocumentedDiagnostic)
{
    const auto& c = GetParam();
    std::vector<std::string> programs = c.programs;
    programs.emplace_back(engineKindName(c.engine));
    for (const auto& p : programs)
        if (!hasProgram(p))
            GTEST_SKIP() << "not installed: " << p;
    for (const auto& f : c.texFiles)
        if (!hasTexFile(f))
            GTEST_SKIP() << "not installed: " << f;

    FixtureCopy fixture("failures/" + c.name);
    auto options = fixture.options();
    options.engine = c.engine;
    auto run = fixture.build(options);

    EXPECT_EQ(run.result.success, c.succeeds) << run.result.lastError;
    auto it = std::ranges::find_if(run.result.diagnostics,
        [&](const Diagnostic& d) { return d.severity == c.severity; });
    ASSERT_NE(it, run.result.diagnostics.end()) << "no diagnostic of the expected severity";
    EXPECT_EQ(it->category, c.category) << it->message;
    EXPECT_EQ(it->file, c.file) << it->message;
    EXPECT_EQ(it->line, c.line) << it->message;
    if (!c.messageContains.empty()) {
        EXPECT_NE(it->message.find(c.messageContains), std::string::npos) << it->message;
    }
    // A failure is explained by real errors, not the "Emergency stop" echo.
    for (const auto& d : run.result.diagnostics)
        EXPECT_FALSE(d.message.starts_with("Emergency stop")) << d.message;
}

INSTANTIATE_TEST_SUITE_P(Failures, FailureFixtureTest, ::testing::ValuesIn(kFailures),
    [](const auto& testParam) {
        auto name = testParam.param.name;
        std::ranges::replace(name, '-', '_');
        return name;
    });

} // namespace
