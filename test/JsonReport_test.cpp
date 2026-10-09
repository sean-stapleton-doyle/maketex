#include "render/JsonReport.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <sstream>

using namespace maketex;
using maketex::cli::printJsonReport;

namespace {

BuildOptions makeOptions()
{
    BuildOptions options;
    options.texFile = "/proj/main.tex";
    return options;
}

// BuildResult has a base class, so it can't take designated initializers.
BuildResult makeResult(DriverResult driver)
{
    BuildResult result;
    static_cast<DriverResult&>(result) = std::move(driver);
    if (result.success)
        result.published.push_back("/proj/main.pdf");
    return result;
}

nlohmann::json report(const BuildResult& result)
{
    std::ostringstream out;
    printJsonReport(result, makeOptions(), out);
    return nlohmann::json::parse(out.str());
}

} // namespace

TEST(JsonReport, successfulBuild)
{
    auto result = makeResult(DriverResult { .success = true, .passes = 2 });
    auto doc = report(result);

    EXPECT_EQ(doc["formatVersion"], 1);
    EXPECT_EQ(doc["success"], true);
    EXPECT_EQ(doc["upToDate"], false);
    EXPECT_EQ(doc["passes"], 2);
    EXPECT_TRUE(doc["error"].is_null());
    EXPECT_EQ(doc["document"], "/proj/main.tex");
    EXPECT_EQ(doc["engine"], "pdflatex");
    EXPECT_EQ(doc["pdf"], "/proj/main.pdf");
    EXPECT_TRUE(doc["diagnostics"].empty());
}

TEST(JsonReport, upToDateBuild)
{
    auto result = makeResult(DriverResult { .success = true, .passes = 0, .upToDate = true });
    EXPECT_EQ(report(result)["upToDate"], true);
}

TEST(JsonReport, toolOnlyRebuildIsNotUpToDate)
{
    auto result = makeResult(DriverResult { .success = true, .passes = 0 });
    EXPECT_EQ(report(result)["upToDate"], false);
}

TEST(JsonReport, pdfIsNullWhenNothingWasPublished)
{
    // e.g. the destination directory is read-only.
    auto result = makeResult(DriverResult { .success = true, .passes = 1 });
    result.published.clear();
    EXPECT_TRUE(report(result)["pdf"].is_null());
}

TEST(JsonReport, detectedEngineAndReason)
{
    auto result = makeResult(DriverResult { .success = true, .passes = 1 });
    result.engine = EngineKind::LuaLaTeX;
    result.engineReason = "loads fontspec";
    auto doc = report(result);
    EXPECT_EQ(doc["engine"], "lualatex");
    EXPECT_EQ(doc["engineReason"], "loads fontspec");
}

TEST(JsonReport, failedBuildCarriesErrorAndNullPdf)
{
    auto result = makeResult(DriverResult { .success = false,
        .passes = 1,
        .lastError = "latex exited with code 1" });
    auto doc = report(result);

    EXPECT_EQ(doc["success"], false);
    EXPECT_EQ(doc["error"], "latex exited with code 1");
    EXPECT_TRUE(doc["pdf"].is_null());
}

TEST(JsonReport, diagnosticFieldsRoundTrip)
{
    auto result = makeResult(DriverResult { .success = false, .passes = 1 });
    result.diagnostics.push_back(Diagnostic {
        .severity = Diagnostic::Severity::Error,
        .file = "chapters/ch1.tex",
        .sourcePath = "/proj/chapters/ch1.tex",
        .line = 9,
        .message = "Undefined control sequence",
        .category = "undefined-macro",
        .rawContext = "l.9 \\bad\n",
    });
    result.diagnostics.push_back(Diagnostic {
        .severity = Diagnostic::Severity::Warning,
        .message = "no location",
    });

    auto diags = report(result)["diagnostics"];
    ASSERT_EQ(diags.size(), 2u);

    EXPECT_EQ(diags[0]["severity"], "error");
    EXPECT_EQ(diags[0]["file"], "chapters/ch1.tex");
    EXPECT_EQ(diags[0]["path"], "/proj/chapters/ch1.tex");
    EXPECT_EQ(diags[0]["line"], 9);
    EXPECT_EQ(diags[0]["message"], "Undefined control sequence");
    EXPECT_EQ(diags[0]["category"], "undefined-macro");
    EXPECT_EQ(diags[0]["context"], "l.9 \\bad\n");

    EXPECT_EQ(diags[1]["severity"], "warning");
    EXPECT_TRUE(diags[1]["file"].is_null());
    EXPECT_TRUE(diags[1]["path"].is_null());
    EXPECT_TRUE(diags[1]["line"].is_null());
}

TEST(JsonReport, messageWithQuotesAndNewlinesStaysValidJson)
{
    auto result = makeResult(DriverResult { .success = false, .passes = 1 });
    result.diagnostics.push_back(Diagnostic {
        .message = "File \"weird\".tex\nnot found",
    });

    // parse() in report() throws on invalid JSON — surviving it is the test.
    auto diags = report(result)["diagnostics"];
    EXPECT_EQ(diags[0]["message"], "File \"weird\".tex\nnot found");
}
