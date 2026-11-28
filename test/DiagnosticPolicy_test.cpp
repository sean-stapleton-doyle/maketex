#include "maketex/DiagnosticPolicy.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using maketex::applyDiagnosticPolicy;
using maketex::Diagnostic;
using maketex::DiagnosticPolicy;

namespace {

Diagnostic makeDiag(Diagnostic::Severity sev, std::string category, std::string msg = "msg")
{
    return Diagnostic {
        .severity = sev,
        .message = std::move(msg),
        .category = std::move(category),
    };
}

} // namespace

TEST(DiagnosticPolicyTest, emptyPolicyLeavesDiagnosticsUntouched)
{
    std::vector<Diagnostic> diags {
        makeDiag(Diagnostic::Severity::Warning, "overfull-hbox"),
        makeDiag(Diagnostic::Severity::Info, "underfull-hbox"),
    };
    auto err = applyDiagnosticPolicy(diags, DiagnosticPolicy { });
    EXPECT_FALSE(err.has_value());
    ASSERT_EQ(diags.size(), 2u);
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[1].severity, Diagnostic::Severity::Info);
}

TEST(DiagnosticPolicyTest, ignoreDropsMatchingNonErrorsButNeverErrors)
{
    std::vector<Diagnostic> diags {
        makeDiag(Diagnostic::Severity::Error, "missing-file", "fatal"),
        makeDiag(Diagnostic::Severity::Warning, "overfull-hbox", "wide"),
        makeDiag(Diagnostic::Severity::Info, "underfull-hbox", "loose"),
        makeDiag(Diagnostic::Severity::Warning, "", "uncategorised"),
        makeDiag(Diagnostic::Severity::Warning, "undefined-citation", "cite"),
    };
    DiagnosticPolicy policy {
        .ignore = { "missing-file", "overfull-hbox", "underfull-hbox" },
    };

    auto err = applyDiagnosticPolicy(diags, policy);
    EXPECT_FALSE(err.has_value());
    ASSERT_EQ(diags.size(), 3u);
    EXPECT_EQ(diags[0].message, "fatal");
    EXPECT_EQ(diags[1].message, "uncategorised");
    EXPECT_EQ(diags[2].message, "cite");
}

TEST(DiagnosticPolicyTest, warningsAsErrorsPromotesSingleAndMultipleWarnings)
{
    {
        std::vector<Diagnostic> diags {
            makeDiag(Diagnostic::Severity::Warning, "package"),
            makeDiag(Diagnostic::Severity::Info, "underfull-hbox"),
        };
        auto err = applyDiagnosticPolicy(diags, DiagnosticPolicy { .warningsAsErrors = true });
        ASSERT_TRUE(err.has_value());
        EXPECT_EQ(*err, "1 warning treated as error");
        EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Error);
        EXPECT_EQ(diags[1].severity, Diagnostic::Severity::Info);
    }
    {
        std::vector<Diagnostic> diags {
            makeDiag(Diagnostic::Severity::Warning, "package"),
            makeDiag(Diagnostic::Severity::Warning, ""),
        };
        auto err = applyDiagnosticPolicy(diags, DiagnosticPolicy { .warningsAsErrors = true });
        ASSERT_TRUE(err.has_value());
        EXPECT_EQ(*err, "2 warnings treated as errors");
        EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Error);
        EXPECT_EQ(diags[1].severity, Diagnostic::Severity::Error);
    }
}

TEST(DiagnosticPolicyTest, failOnPromotesMatchingCategoriesOnlyAndSkipsUncategorised)
{
    std::vector<Diagnostic> diags {
        makeDiag(Diagnostic::Severity::Warning, "", "no-cat"),
        makeDiag(Diagnostic::Severity::Warning, "overfull-hbox", "box"),
        makeDiag(Diagnostic::Severity::Warning, "undefined-citation", "cite1"),
        makeDiag(Diagnostic::Severity::Warning, "undefined-reference", "ref1"),
    };
    DiagnosticPolicy policy {
        .failOn = { "undefined-citation", "undefined-reference" },
    };

    auto err = applyDiagnosticPolicy(diags, policy);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(*err, "failed on undefined-citation warnings");
    EXPECT_EQ(diags[0].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[1].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(diags[2].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(diags[3].severity, Diagnostic::Severity::Error);
}

TEST(DiagnosticPolicyTest, failOnSingleWarningUsesSingularMessage)
{
    std::vector<Diagnostic> diags {
        makeDiag(Diagnostic::Severity::Warning, "undefined-reference"),
    };
    auto err = applyDiagnosticPolicy(diags, DiagnosticPolicy { .failOn = { "undefined-reference" } });
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(*err, "failed on undefined-reference warning");
}

TEST(DiagnosticPolicyTest, ignoreRunsBeforePromotion)
{
    std::vector<Diagnostic> diags {
        makeDiag(Diagnostic::Severity::Warning, "overfull-hbox"),
    };
    DiagnosticPolicy policy {
        .warningsAsErrors = true,
        .ignore = { "overfull-hbox" },
    };
    auto err = applyDiagnosticPolicy(diags, policy);
    EXPECT_FALSE(err.has_value());
    EXPECT_TRUE(diags.empty());
}
