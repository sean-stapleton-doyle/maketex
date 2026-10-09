#include "render/GithubReport.h"

#include <gtest/gtest.h>

#include <sstream>

using namespace maketex;
using maketex::cli::printGithubReport;

namespace {

std::string report(const DriverResult& result)
{
    std::ostringstream out;
    printGithubReport(result, out);
    return out.str();
}

} // namespace

TEST(GithubReport, errorAnnotationWithFileAndLine)
{
    DriverResult result { .success = false,
        .passes = 1,
        .lastError = "latex exited with code 1" };
    result.diagnostics.push_back(Diagnostic {
        .severity = Diagnostic::Severity::Error,
        .file = "chapters/ch1.tex",
        .line = 9,
        .message = "Undefined control sequence",
    });

    EXPECT_EQ(report(result),
        "::error file=chapters/ch1.tex,line=9,title=LaTeX"
        "::Undefined control sequence\n"
        "maketex: failed after 1 pass: latex exited with code 1\n");
}

TEST(GithubReport, warningWithoutLocationOmitsProperties)
{
    DriverResult result { .success = true, .passes = 1 };
    result.diagnostics.push_back(Diagnostic {
        .severity = Diagnostic::Severity::Warning,
        .message = "something loose",
    });

    EXPECT_EQ(report(result),
        "::warning title=LaTeX::something loose\n"
        "maketex: done in 1 pass\n");
}

TEST(GithubReport, messageEscapesPercentAndNewlines)
{
    DriverResult result { .success = true, .passes = 1 };
    result.diagnostics.push_back(Diagnostic {
        .severity = Diagnostic::Severity::Warning,
        .message = "50% done\nnext",
    });

    auto out = report(result);
    EXPECT_NE(out.find("50%25 done%0Anext"), std::string::npos);
}

TEST(GithubReport, filePropertyEscapesCommaAndColon)
{
    DriverResult result { .success = true, .passes = 1 };
    result.diagnostics.push_back(Diagnostic {
        .severity = Diagnostic::Severity::Warning,
        .file = "odd,name:file.tex",
        .message = "m",
    });

    auto out = report(result);
    EXPECT_NE(out.find("file=odd%2Cname%3Afile.tex"), std::string::npos);
}

TEST(GithubReport, upToDateSummary)
{
    DriverResult result { .success = true, .passes = 0 };
    EXPECT_EQ(report(result), "maketex: up to date\n");
}
