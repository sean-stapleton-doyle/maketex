#include "maketex/Blake3Hasher.h"
#include "maketex/BuildDriver.h"
#include "maketex/Launch.h"
#include "maketex/ProcessRunner.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolInvocation.h"
#include "maketex/engines/PdfLatexEngine.h"
#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using maketex::Blake3Hasher;
using maketex::BuildDriver;
using maketex::EngineArguments;
using maketex::EngineInvocation;
using maketex::IEngine;
using maketex::InvokerKind;
using maketex::ITool;
using maketex::launchProcess;
using maketex::PdfLatexEngine;
using maketex::ProcessLaunchSpec;
using maketex::ProcessResult;
using maketex::ProcessRunner;
using maketex::RunResult;
using maketex::StateDatabase;
using maketex::ToolContext;
using maketex::ToolInput;
using maketex::ToolInvocation;
using maketex::WaitLimits;

namespace {

class ProcessRunnerTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_process_runner_test"; }
};

class ScriptedEngine final : public IEngine {
public:
    explicit ScriptedEngine(std::filesystem::path dir)
        : m_dir(std::move(dir))
    {
    }

    RunResult<EngineInvocation> run(const EngineInvocation& inv, const WaitLimits&) override
    {
        auto bcf = m_dir / "main.bcf";
        std::ofstream(bcf) << "bcf v1\n";
        std::ofstream(m_dir / "main.fls")
            << "PWD " << m_dir.string() << "\n"
            << "INPUT " << (m_dir / "main.tex").string() << "\n"
            << "OUTPUT " << bcf.string() << "\n";
        std::ofstream(m_dir / "main.log") << "This is pdfTeX\n";
        return RunResult<EngineInvocation> {
            .invocation = inv,
            .kind = InvokerKind::PdfLaTeX,
            .exitCode = 0,
        };
    }

    std::string_view name() const override { return "fake"; }
    InvokerKind invokerKind() const override { return InvokerKind::PdfLaTeX; }

private:
    std::filesystem::path m_dir;
};

class SimpleTool final : public ITool {
public:
    SimpleTool(std::filesystem::path trigger, std::filesystem::path output)
        : m_trigger(std::move(trigger))
        , m_output(std::move(output))
    {
    }

    std::string_view name() const override { return "simple-tool"; }
    InvokerKind invokerKind() const override { return InvokerKind::Biber; }
    std::vector<std::string> buildArgs(const ToolInvocation&) const override
    {
        return { "simple-tool", "--run" };
    }
    bool applies(const ToolContext& ctx) const override { return ctx.wrote(m_trigger); }
    std::vector<ToolInput> inputs(const ToolContext&) const override
    {
        return { ToolInput { .path = m_trigger } };
    }
    std::vector<std::filesystem::path> outputs(const ToolContext&) const override
    {
        return { m_output };
    }

private:
    std::filesystem::path m_trigger;
    std::filesystem::path m_output;
};

} // namespace

TEST_F(ProcessRunnerTest, launchProcessForwardsSpecAndCapturesResult)
{
    ProcessLaunchSpec captured;
    ProcessRunner runner = [&](const ProcessLaunchSpec& spec) {
        captured = spec;
        return ProcessResult {
            .exitCode = 0,
            .stdoutOutput = "ok\n",
            .stderrOutput = "warn\n",
        };
    };

    std::vector<std::string> argv { "biber", "main" };
    std::vector<std::pair<std::string, std::string>> env { { "BIBINPUTS", "/project:" } };
    WaitLimits limits { .timeout = std::chrono::milliseconds(500) };

    auto res = launchProcess(runner, argv, m_dir, InvokerKind::Biber,
        ToolInvocation { .jobName = "main" }, env, limits);

    EXPECT_TRUE(res.success());
    EXPECT_EQ(res.program, "biber");
    EXPECT_EQ(res.kind, InvokerKind::Biber);
    EXPECT_EQ(res.stdoutOutput, "ok\n");
    EXPECT_EQ(res.stderrOutput, "warn\n");
    EXPECT_EQ(captured.argv, argv);
    EXPECT_EQ(captured.workingDir, m_dir);
    EXPECT_EQ(captured.extraEnv, env);
    EXPECT_EQ(captured.limits.timeout, limits.timeout);
}

TEST_F(ProcessRunnerTest, launchProcessTranslatesEnoentWithExistingWorkDirToNotFound)
{
    ProcessRunner runner = [](const ProcessLaunchSpec&) -> ProcessResult {
        throw std::system_error(ENOENT, std::generic_category(), "spawn");
    };

    auto res = launchProcess(runner, { "missing-tool" }, m_dir, InvokerKind::Bibtex,
        ToolInvocation { });

    EXPECT_FALSE(res.success());
    EXPECT_TRUE(res.notFound);
    EXPECT_EQ(res.exitCode, 127);
    EXPECT_EQ(res.program, "missing-tool");
}

TEST_F(ProcessRunnerTest, launchProcessRethrowsEnoentWhenWorkDirIsMissing)
{
    ProcessRunner runner = [](const ProcessLaunchSpec&) -> ProcessResult {
        throw std::system_error(ENOENT, std::generic_category(), "spawn");
    };

    EXPECT_THROW(
        launchProcess(runner, { "biber" }, m_dir / "no-such-dir", InvokerKind::Biber,
            ToolInvocation { }),
        std::system_error);
}

TEST_F(ProcessRunnerTest, launchProcessRethrowsNonEnoentErrors)
{
    ProcessRunner runner = [](const ProcessLaunchSpec&) -> ProcessResult {
        throw std::system_error(EACCES, std::generic_category(), "spawn");
    };

    EXPECT_THROW(
        launchProcess(runner, { "biber" }, m_dir, InvokerKind::Biber, ToolInvocation { }),
        std::system_error);
}

TEST_F(ProcessRunnerTest, launchProcessPropagatesTimeoutAndCancellationFlags)
{
    ProcessRunner timedOut = [](const ProcessLaunchSpec&) {
        return ProcessResult { .exitCode = -1, .timedOut = true };
    };
    auto r1 = launchProcess(timedOut, { "pdflatex" }, { }, InvokerKind::PdfLaTeX,
        EngineInvocation { });
    EXPECT_TRUE(r1.timedOut);
    EXPECT_FALSE(r1.cancelled);
    EXPECT_FALSE(r1.success());

    ProcessRunner cancelled = [](const ProcessLaunchSpec&) {
        return ProcessResult { .exitCode = -1, .cancelled = true };
    };
    auto r2 = launchProcess(cancelled, { }, { }, InvokerKind::PdfLaTeX, EngineInvocation { });
    EXPECT_TRUE(r2.cancelled);
    EXPECT_TRUE(r2.program.empty());
}

TEST_F(ProcessRunnerTest, engineBaseUsesInjectedProcessRunnerAndMergesLogWidthEnv)
{
    ProcessLaunchSpec captured;
    ProcessRunner runner = [&](const ProcessLaunchSpec& spec) {
        captured = spec;
        return ProcessResult { .exitCode = 0, .stdoutOutput = "This is pdfTeX\n" };
    };

    PdfLatexEngine engine(EngineArguments { }, runner);
    EngineInvocation inv {
        .texFile = m_dir / "doc.tex",
        .outputDir = m_dir,
        .extraEnv = { { "max_print_line", "5000" }, { "TEXINPUTS", "/project:" } },
    };

    auto res = engine.run(inv, { });
    EXPECT_TRUE(res.success());
    EXPECT_EQ(res.stdoutOutput, "This is pdfTeX\n");
    EXPECT_EQ(captured.workingDir, m_dir);
    EXPECT_EQ(captured.argv.front(), "pdflatex");

    auto findEnv = [&](std::string_view key) -> std::optional<std::string> {
        auto it = std::ranges::find(captured.extraEnv, key,
            &std::pair<std::string, std::string>::first);
        if (it == captured.extraEnv.end())
            return std::nullopt;
        return it->second;
    };
    EXPECT_EQ(findEnv("max_print_line"), "5000");
    EXPECT_EQ(findEnv("error_line"), "254");
    EXPECT_EQ(findEnv("half_error_line"), "238");
    EXPECT_EQ(findEnv("TEXINPUTS"), "/project:");
}

TEST_F(ProcessRunnerTest, buildDriverUsesInjectedProcessRunnerAndClockForTools)
{
    writeFile("main.tex", "\\documentclass{article}\\begin{document}Hi\\end{document}");
    auto bcf = m_dir / "main.bcf";
    auto bbl = m_dir / "main.bbl";

    ScriptedEngine engine(m_dir);
    SimpleTool tool(bcf, bbl);
    StateDatabase db(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;

    int runnerCalls = 0;
    BuildDriver::Config cfg {
        .tools = { &tool },
        .processRunner = [&](const ProcessLaunchSpec& spec) {
            ++runnerCalls;
            EXPECT_EQ(spec.argv, (std::vector<std::string> { "simple-tool", "--run" }));
            std::ofstream(bbl) << "generated bbl\n";
            return ProcessResult { .exitCode = 0 }; },
        .clock = [] { return std::int64_t { 1700000123 }; },
    };

    BuildDriver driver(engine, db, hasher,
        EngineInvocation { .texFile = m_dir / "main.tex", .outputDir = m_dir },
        std::move(cfg));

    auto res = driver.run();
    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(runnerCalls, 1);
    auto rule = db.getRule("tool:simple-tool:main");
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->ran_at, 1700000123);
    EXPECT_TRUE(rule->succeeded);
}
