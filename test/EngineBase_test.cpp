#include "maketex/EngineBase.h"
#include "maketex/engines/LuaLatexEngine.h"
#include "maketex/engines/PdfLatexEngine.h"
#include "maketex/engines/XeLatexEngine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace maketex;

namespace {

// Engine that runs /bin/echo so we can test run() without a LaTeX install.
class EchoEngine final : public EngineBase {
public:
    std::string_view name() const override { return "echo"; }
    InvokerKind invokerKind() const override { return InvokerKind::PdfLaTeX; }
    std::vector<std::string> buildArgs(const EngineInvocation& inv) const override
    {
        return { "/bin/echo", inv.texFile.string() };
    }
};

class EngineBaseTest : public ::testing::Test { };

} // namespace

// ── PdfLatexEngine::buildArgs ─────────────────────────────────────────────────

TEST_F(EngineBaseTest, pdflatexFirstArgIsBinary)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    EXPECT_EQ(engine.buildArgs(inv).front(), "pdflatex");
}

TEST_F(EngineBaseTest, pdflatexAlwaysAddsRecorder)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    EXPECT_NE(std::ranges::find(args, "-recorder"), args.end());
}

TEST_F(EngineBaseTest, pdflatexAlwaysNonstopmode)
{
    // nonstopmode (not batchmode): errors appear on stdout so DiagnosticExtractor
    // can parse them from the captured output without reading the .log file.
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    EXPECT_NE(std::ranges::find(args, "-interaction=nonstopmode"), args.end());
    EXPECT_EQ(std::ranges::find(args, "-interaction=batchmode"), args.end());
}

TEST_F(EngineBaseTest, pdflatexHaltOnErrorPresent)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    inv.haltOnError = true;
    auto args = engine.buildArgs(inv);
    EXPECT_NE(std::ranges::find(args, "-halt-on-error"), args.end());
}

TEST_F(EngineBaseTest, pdflatexHaltOnErrorAbsent)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    inv.haltOnError = false;
    auto args = engine.buildArgs(inv);
    EXPECT_EQ(std::ranges::find(args, "-halt-on-error"), args.end());
}

TEST_F(EngineBaseTest, pdflatexShellEscape)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    inv.shellEscape = true;
    auto args = engine.buildArgs(inv);
    EXPECT_NE(std::ranges::find(args, "-shell-escape"), args.end());
}

TEST_F(EngineBaseTest, pdflatexSynctex)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    inv.synctex = true;
    auto args = engine.buildArgs(inv);
    EXPECT_NE(std::ranges::find(args, "-synctex=1"), args.end());
}

TEST_F(EngineBaseTest, pdflatexOutputDirectory)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    inv.outputDir = "/tmp/out";
    auto args = engine.buildArgs(inv);
    EXPECT_NE(std::ranges::find(args, "-output-directory=/tmp/out"), args.end());
}

TEST_F(EngineBaseTest, pdflatexJobName)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    inv.jobName = "mydoc";
    auto args = engine.buildArgs(inv);
    EXPECT_NE(std::ranges::find(args, "-jobname=mydoc"), args.end());
}

TEST_F(EngineBaseTest, pdflatexTexFileIsLast)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    EXPECT_EQ(engine.buildArgs(inv).back(), "doc.tex");
}

TEST_F(EngineBaseTest, pdflatexNoOutputDirByDefault)
{
    PdfLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    EXPECT_TRUE(std::ranges::none_of(args, [](const std::string& s) {
        return s.starts_with("-output-directory");
    }));
}

// ── XeLatexEngine / LuaLatexEngine::buildArgs ─────────────────────────────────

TEST_F(EngineBaseTest, xelatexFirstArgIsBinaryAndCoreFlagsPresent)
{
    XeLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    EXPECT_EQ(args.front(), "xelatex");
    EXPECT_NE(std::ranges::find(args, "-recorder"), args.end());
    EXPECT_NE(std::ranges::find(args, "-interaction=nonstopmode"), args.end());
    EXPECT_EQ(args.back(), "doc.tex");
}

TEST_F(EngineBaseTest, xelatexRunsDirectToPdf)
{
    // Unlike latexmk we do not split into xelatex -no-pdf + xdvipdfmx;
    // xelatex produces the PDF itself.
    XeLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    EXPECT_EQ(std::ranges::find(args, "-no-pdf"), args.end());
}

TEST_F(EngineBaseTest, xelatexInvokerKind)
{
    EXPECT_EQ(XeLatexEngine().invokerKind(), InvokerKind::XeLaTeX);
}

TEST_F(EngineBaseTest, lualatexFirstArgIsBinaryAndCoreFlagsPresent)
{
    LuaLatexEngine engine;
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    EXPECT_EQ(args.front(), "lualatex");
    EXPECT_NE(std::ranges::find(args, "-recorder"), args.end());
    EXPECT_NE(std::ranges::find(args, "-interaction=nonstopmode"), args.end());
    EXPECT_EQ(args.back(), "doc.tex");
}

TEST_F(EngineBaseTest, lualatexInvokerKind)
{
    EXPECT_EQ(LuaLatexEngine().invokerKind(), InvokerKind::LuaLaTeX);
}

// ── EngineArguments ───────────────────────────────────────────────────────────

TEST_F(EngineBaseTest, engineArgumentsProgramOverridesBinary)
{
    PdfLatexEngine engine(EngineArguments { .program = "/opt/tex/pdflatex" });
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    EXPECT_EQ(engine.buildArgs(inv).front(), "/opt/tex/pdflatex");
}

TEST_F(EngineBaseTest, engineArgumentsExtraOptionsBeforeTexFile)
{
    XeLatexEngine engine(EngineArguments { .extraOptions = { "-8bit" } });
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    auto it = std::ranges::find(args, "-8bit");
    ASSERT_NE(it, args.end());
    EXPECT_EQ(args.back(), "doc.tex");
}

TEST_F(EngineBaseTest, engineArgumentsConflictingKeyLosesToEngineFlag)
{
    // -interaction is engine-managed: the user's value must not appear, and
    // the engine's must survive.
    PdfLatexEngine engine(
        EngineArguments { .extraOptions = { "-interaction=batchmode" } });
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    EXPECT_EQ(std::ranges::find(args, "-interaction=batchmode"), args.end());
    EXPECT_NE(std::ranges::find(args, "-interaction=nonstopmode"), args.end());
}

TEST_F(EngineBaseTest, engineArgumentsExactDuplicateFlagDropped)
{
    PdfLatexEngine engine(EngineArguments { .extraOptions = { "-recorder" } });
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    EXPECT_EQ(std::ranges::count(args, std::string("-recorder")), 1);
}

TEST_F(EngineBaseTest, engineArgumentsRepeatedUserOptionsPassThrough)
{
    // Options the engine does not manage may repeat (e.g. -cnf-line);
    // dedup only runs against the engine-built prefix.
    LuaLatexEngine engine(EngineArguments {
        .extraOptions = { "-cnf-line=max_print_line=1000", "-cnf-line=error_line=254" } });
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    auto args = engine.buildArgs(inv);
    EXPECT_NE(std::ranges::find(args, "-cnf-line=max_print_line=1000"), args.end());
    EXPECT_NE(std::ranges::find(args, "-cnf-line=error_line=254"), args.end());
}

TEST_F(EngineBaseTest, engineArgumentsConflictNotTriggeredWhenEngineFlagAbsent)
{
    // synctex is off in the invocation, so the engine emits no -synctex flag
    // and the user's spelling goes through.
    PdfLatexEngine engine(EngineArguments { .extraOptions = { "-synctex=15" } });
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    inv.synctex = false;
    auto args = engine.buildArgs(inv);
    EXPECT_NE(std::ranges::find(args, "-synctex=15"), args.end());
}

// ── EngineBase::run() ─────────────────────────────────────────────────────────

TEST_F(EngineBaseTest, runReturnsZeroExitAndCapturesStdout)
{
    EchoEngine engine;
    EngineInvocation inv;
    inv.texFile = "hello.tex";

    auto result = engine.run(inv, { });

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.stdoutOutput, "hello.tex\n");
    EXPECT_TRUE(result.stderrOutput.empty());
}

TEST_F(EngineBaseTest, runPropagatesNonZeroExit)
{
    class FalseEngine final : public EngineBase {
    public:
        using EngineBase::EngineBase;
        std::string_view name() const override { return "false"; }
        InvokerKind invokerKind() const override { return InvokerKind::PdfLaTeX; }
        std::vector<std::string> buildArgs(const EngineInvocation&) const override
        {
            return { "/usr/bin/false" };
        }
    };

    FalseEngine engine;
    auto result = engine.run(EngineInvocation { }, { });
    EXPECT_EQ(result.exitCode, 1);
}

// ── -file-line-error ──────────────────────────────────────────────────────────

TEST_F(EngineBaseTest, allEnginesAddFileLineError)
{
    // file:line: error markers are the primary source for per-file error
    // attribution in DiagnosticExtractor; every engine must emit them.
    EngineInvocation inv;
    inv.texFile = "doc.tex";
    for (auto args : { PdfLatexEngine().buildArgs(inv),
             XeLatexEngine().buildArgs(inv), LuaLatexEngine().buildArgs(inv) })
        EXPECT_NE(std::ranges::find(args, "-file-line-error"), args.end());
}

TEST_F(EngineBaseTest, runSetsLogWidthEnvironment)
{
    // EngineBase::run must raise TeX's log line width so file paths and
    // messages are never wrapped mid-token (see DiagnosticExtractor).
    class EnvEngine final : public EngineBase {
    public:
        std::string_view name() const override { return "env"; }
        InvokerKind invokerKind() const override { return InvokerKind::PdfLaTeX; }
        std::vector<std::string> buildArgs(const EngineInvocation&) const override
        {
            return { "/bin/sh", "-c", "echo $max_print_line" };
        }
    };

    EnvEngine engine;
    auto result = engine.run(EngineInvocation { }, { });
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(result.stdoutOutput, "10000\n");
}
