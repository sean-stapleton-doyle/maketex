#include "EngineBase.h"
#include "engines/PdfLatexEngine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace maketex;

// Engine that runs /bin/echo so we can test run() without a LaTeX install.
class EchoEngine final : public EngineBase {
public:
    std::string_view name() const override { return "echo"; }
    InvokerKind invokerKind() const override { return InvokerKind::PdfLatex; }
    std::vector<std::string> buildArgs(const EngineInvocation& inv) const override
    {
        return { "/bin/echo", inv.texFile.string() };
    }
};

class EngineBaseTest : public ::testing::Test { };

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

// ── EngineBase::run() ─────────────────────────────────────────────────────────

TEST_F(EngineBaseTest, runReturnsZeroExitAndCapturesStdout)
{
    EchoEngine engine;
    EngineInvocation inv;
    inv.texFile = "hello.tex";

    auto result = engine.run(inv);

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
        InvokerKind invokerKind() const override { return InvokerKind::PdfLatex; }
        std::vector<std::string> buildArgs(const EngineInvocation&) const override
        {
            return { "/usr/bin/false" };
        }
    };

    FalseEngine engine;
    auto result = engine.run(EngineInvocation {});
    EXPECT_EQ(result.exitCode, 1);
}
