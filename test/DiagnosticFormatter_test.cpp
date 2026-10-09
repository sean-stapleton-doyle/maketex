#include "maketex/DiagnosticFormatter.h"

#include <gtest/gtest.h>

#include <sstream>

using maketex::Diagnostic;
namespace diagnostics = maketex::diagnostics;

namespace {

std::string formatted(const Diagnostic& d, diagnostics::Verbosity verbosity)
{
    std::ostringstream out;
    diagnostics::format({ d }, out, verbosity);
    return out.str();
}

} // namespace

TEST(DiagnosticFormatter, locationMessageAndHint)
{
    Diagnostic d {
        .severity = Diagnostic::Severity::Error,
        .file = "main.tex",
        .line = 19,
        .message = "File `minted.sty' not found",
        .rawContext = "raw log",
        .hint = "install it: sudo tlmgr install minted",
    };
    EXPECT_EQ(formatted(d, diagnostics::Verbosity::Normal),
        "error: main.tex:19: File `minted.sty' not found\n"
        "  hint: install it: sudo tlmgr install minted\n");
}

TEST(DiagnosticFormatter, verboseAddsRawContext)
{
    Diagnostic d { .severity = Diagnostic::Severity::Warning, .message = "m", .rawContext = "ctx" };
    EXPECT_EQ(formatted(d, diagnostics::Verbosity::Verbose), "warning: m\nctx\n");
}

TEST(DiagnosticFormatter, infoReadsAsNote)
{
    Diagnostic d { .severity = Diagnostic::Severity::Info, .message = "rebuilt" };
    EXPECT_EQ(formatted(d, diagnostics::Verbosity::Normal), "note: rebuilt\n");
}
