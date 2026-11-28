#include "maketex/Blake3Hasher.h"
#include "maketex/BuildDriver.h"
#include "maketex/FileStateTracker.h"
#include "maketex/IFileHasher.h"
#include "maketex/ITool.h"
#include "maketex/ProcessRunner.h"
#include "maketex/StateDatabase.h"
#include "maketex/driver/FileDigests.h"
#include "maketex/driver/ToolPass.h"
#include "maketex/graph/BuildGraph.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"
#include "test_utils.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <vector>

using maketex::AuxRecord;
using maketex::Blake3Hasher;
using maketex::BuildDriver;
using maketex::BuildEvent;
using maketex::BuildGraph;
using maketex::BuildObserver;
using maketex::DiagnosticPolicy;
using maketex::EngineInvocation;
using maketex::FileRole;
using maketex::FileStateTracker;
using maketex::FlsRecord;
using maketex::Hash;
using maketex::IEngine;
using maketex::IFileHasher;
using maketex::InvokerKind;
using maketex::ITool;
using maketex::LogRecord;
using maketex::ProcessLaunchSpec;
using maketex::ProcessResult;
using maketex::ProcessRunner;
using maketex::RunResult;
using maketex::StateDatabase;
using maketex::ToolContext;
using maketex::ToolInput;
using maketex::ToolInvocation;
using maketex::ToolWorkingDir;
using maketex::WaitLimits;
using maketex::driver::FileDigests;
using maketex::driver::makeToolInvocation;
using maketex::driver::ToolPass;
using maketex::driver::toolRuleName;

namespace fs = std::filesystem;

namespace {

class StubTool final : public ITool {
public:
    StubTool(std::string toolName,
        std::vector<ToolInput> toolInputs,
        std::vector<fs::path> toolOutputs,
        InvokerKind kind = InvokerKind::Biber,
        ToolWorkingDir workDirPref = ToolWorkingDir::Output,
        bool appliesFlag = true)
        : m_name(std::move(toolName))
        , m_inputs(std::move(toolInputs))
        , m_outputs(std::move(toolOutputs))
        , m_kind(kind)
        , m_workDirPref(workDirPref)
        , m_applies(appliesFlag)
    {
    }

    std::string_view name() const override { return m_name; }
    InvokerKind invokerKind() const override { return m_kind; }
    ToolWorkingDir workingDirPreference() const override { return m_workDirPref; }
    std::vector<std::string> buildArgs(const ToolInvocation&) const override
    {
        return { m_name, "--flag" };
    }
    bool applies(const ToolContext&) const override { return m_applies; }
    std::vector<ToolInput> inputs(const ToolContext&) const override { return m_inputs; }
    std::vector<fs::path> outputs(const ToolContext&) const override { return m_outputs; }

private:
    std::string m_name;
    std::vector<ToolInput> m_inputs;
    std::vector<fs::path> m_outputs;
    InvokerKind m_kind;
    ToolWorkingDir m_workDirPref;
    bool m_applies;
};

class FailingHasher final : public IFileHasher {
public:
    explicit FailingHasher(fs::path failPath)
        : m_failPath(std::move(failPath))
    {
    }

    std::expected<Hash, std::error_code> hashFile(
        const fs::path& p, const std::optional<std::regex>& regex) const override
    {
        if (fs::weakly_canonical(p) == fs::weakly_canonical(m_failPath))
            return std::unexpected(std::make_error_code(std::errc::permission_denied));
        return m_real.hashFile(p, regex);
    }

    Hash hashBytes(std::string_view bytes) const override { return m_real.hashBytes(bytes); }

private:
    fs::path m_failPath;
    Blake3Hasher m_real;
};

class ScriptedDriverEngine final : public IEngine {
public:
    explicit ScriptedDriverEngine(std::function<RunResult<EngineInvocation>(const EngineInvocation&)> fn)
        : m_fn(std::move(fn))
    {
    }

    RunResult<EngineInvocation> run(const EngineInvocation& inv, const WaitLimits&) override
    {
        ++m_calls;
        return m_fn(inv);
    }

    std::string_view name() const override { return "xelatex"; }
    InvokerKind invokerKind() const override { return InvokerKind::XeLaTeX; }
    int calls() const { return m_calls; }

private:
    std::function<RunResult<EngineInvocation>(const EngineInvocation&)> m_fn;
    int m_calls = 0;
};

class ToolPassTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_tool_pass_test"; }

    EngineInvocation makeInv() const
    {
        return EngineInvocation {
            .texFile = m_dir / "main.tex",
            .outputDir = m_dir / "out",
        };
    }

    static ToolContext makeEmptyCtx(const EngineInvocation& inv)
    {
        return ToolContext(
            inv,
            [] { return LogRecord { }; },
            [] { return AuxRecord { }; },
            [] { return FlsRecord { }; });
    }
};

} // namespace

TEST_F(ToolPassTest, makeToolInvocationRespectsWorkingDirPreference)
{
    auto inv = makeInv();
    StubTool outTool("biber", { }, { }, InvokerKind::Biber, ToolWorkingDir::Output);
    StubTool srcTool("custom", { }, { }, InvokerKind::Custom, ToolWorkingDir::Source);

    auto outInv = makeToolInvocation(inv, outTool);
    EXPECT_EQ(outInv.workingDir, inv.outputDir);
    EXPECT_EQ(outInv.sourceDir, m_dir);
    EXPECT_EQ(outInv.jobName, "main");

    auto srcInv = makeToolInvocation(inv, srcTool);
    EXPECT_EQ(srcInv.workingDir, m_dir);
    EXPECT_EQ(toolRuleName("biber", "main"), "tool:biber:main");
}

TEST_F(ToolPassTest, fileDigestsHandlesDerivedAndMissingInputs)
{
    auto existing = writeFile("a.txt", "hello");
    auto missing = m_dir / "missing.txt";

    StateDatabase db(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;
    FileStateTracker tracker(db, hasher);
    BuildGraph graph;
    FileDigests digests(graph, tracker, hasher);

    std::vector<ToolInput> inputs {
        ToolInput { .path = existing },
        ToolInput { .path = missing },
        ToolInput { .path = m_dir / "main.aux#bib", .content = std::string("cite{a}") },
    };
    auto res = digests.digestInputs(inputs);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->size(), 2u);
    EXPECT_TRUE(res->contains(FileDigests::canonicalKey(existing)));
    EXPECT_TRUE(res->contains(FileDigests::canonicalKey(m_dir / "main.aux#bib")));
    EXPECT_FALSE(res->contains(FileDigests::canonicalKey(missing)));
}

TEST_F(ToolPassTest, unchangedOutputDoesNotMarkOutputsChanged)
{
    fs::create_directories(m_dir / "out");
    auto inFile = writeFile("out/main.bcf", "bcf v1");
    auto outFile = writeFile("out/main.bbl", "stable bbl");

    StateDatabase db(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;
    FileStateTracker tracker(db, hasher);
    BuildGraph graph;
    FileDigests digests(graph, tracker, hasher);

    StubTool tool("biber", { { .path = inFile } }, { outFile });
    std::vector<ITool*> tools { &tool };
    auto inv = makeInv();
    auto ctx = makeEmptyCtx(inv);

    ProcessRunner runner = [](const ProcessLaunchSpec&) {
        return ProcessResult { .exitCode = 0, .stdoutOutput = "WARN - I didn't find a database entry for 'x'\n" };
    };
    WaitLimits limits;
    BuildObserver observer;
    ToolPass pass({ db, digests, runner, limits, observer, [] { return std::int64_t { 42 }; } }, inv);

    auto outcome = pass.run(tools, ctx);
    EXPECT_TRUE(outcome.ranAny);
    EXPECT_FALSE(outcome.outputsChanged);
    EXPECT_TRUE(outcome.changes.empty());
    EXPECT_FALSE(outcome.failure.has_value());
    ASSERT_EQ(outcome.diagnostics.size(), 1u);
    EXPECT_EQ(outcome.diagnostics[0].category, "undefined-citation");

    auto rule = db.getRule("tool:biber:main");
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->ran_at, 42);
    EXPECT_TRUE(rule->succeeded);
}

TEST_F(ToolPassTest, unknownOutputsAlwaysTriggerRerunWithDescriptiveReason)
{
    fs::create_directories(m_dir / "out");
    auto inFile = writeFile("out/main.pytxcode", "code");

    StateDatabase db(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;
    FileStateTracker tracker(db, hasher);
    BuildGraph graph;
    FileDigests digests(graph, tracker, hasher);

    StubTool tool("pythontex", { { .path = inFile } }, { }, InvokerKind::Pythontex);
    std::vector<ITool*> tools { &tool };
    auto inv = makeInv();
    auto ctx = makeEmptyCtx(inv);

    ProcessRunner runner = [](const ProcessLaunchSpec&) { return ProcessResult { .exitCode = 0 }; };
    WaitLimits limits;
    BuildObserver observer;
    ToolPass pass({ db, digests, runner, limits, observer, { } }, inv);

    auto outcome = pass.run(tools, ctx);
    EXPECT_TRUE(outcome.ranAny);
    EXPECT_TRUE(outcome.outputsChanged);
    EXPECT_EQ(outcome.changes,
        (std::vector<std::string> { "pythontex ran (its outputs are not tracked)" }));
}

TEST_F(ToolPassTest, parallelWaveFailureStillRecordsSiblingSuccess)
{
    fs::create_directories(m_dir / "out");
    auto in1 = writeFile("out/a.in", "1");
    auto in2 = writeFile("out/b.in", "2");
    auto out1 = m_dir / "out" / "a.out";
    auto out2 = m_dir / "out" / "b.out";

    StateDatabase db(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;
    FileStateTracker tracker(db, hasher);
    BuildGraph graph;
    FileDigests digests(graph, tracker, hasher);

    StubTool failing("tool-fail", { { .path = in1 } }, { out1 });
    StubTool succeeding("tool-ok", { { .path = in2 } }, { out2 });
    std::vector<ITool*> tools { &failing, &succeeding };
    auto inv = makeInv();
    auto ctx = makeEmptyCtx(inv);

    ProcessRunner runner = [&](const ProcessLaunchSpec& spec) {
        if (spec.argv.front() == "tool-fail")
            return ProcessResult { .exitCode = 1, .stderrOutput = "ERROR - boom\n" };
        std::ofstream(out2) << "ok-data";
        return ProcessResult { .exitCode = 0 };
    };
    WaitLimits limits;
    std::vector<BuildEvent> events;
    BuildObserver observer = [&](const BuildEvent& ev) { events.push_back(ev); };
    ToolPass pass({ db, digests, runner, limits, observer, { } }, inv);

    auto outcome = pass.run(tools, ctx);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failedToolName, "tool-fail");
    EXPECT_TRUE(outcome.ranAny);
    EXPECT_TRUE(outcome.outputsChanged);

    auto failRule = db.getRule("tool:tool-fail:main");
    ASSERT_TRUE(failRule.has_value());
    EXPECT_FALSE(failRule->succeeded);

    auto okRule = db.getRule("tool:tool-ok:main");
    ASSERT_TRUE(okRule.has_value());
    EXPECT_TRUE(okRule->succeeded);

    EXPECT_EQ(events.size(), 4u);
}

TEST_F(ToolPassTest, waveTwoFailurePreservesWaveOneRecordAndNotFoundMapsTo127)
{
    fs::create_directories(m_dir / "out");
    auto in1 = writeFile("out/step1.in", "1");
    auto mid = m_dir / "out" / "step1.out";
    auto out2 = m_dir / "out" / "step2.out";

    StateDatabase db(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;
    FileStateTracker tracker(db, hasher);
    BuildGraph graph;
    FileDigests digests(graph, tracker, hasher);

    StubTool wave1("step1", { { .path = in1 } }, { mid });
    StubTool wave2("step2", { { .path = mid } }, { out2 });
    std::vector<ITool*> tools { &wave1, &wave2 };
    auto inv = makeInv();
    auto ctx = makeEmptyCtx(inv);

    ProcessRunner runner = [&](const ProcessLaunchSpec& spec) -> ProcessResult {
        if (spec.argv.front() == "step1") {
            std::ofstream(mid) << "from-step1";
            return ProcessResult { .exitCode = 0 };
        }
        throw std::system_error(ENOENT, std::generic_category(), "spawn");
    };
    WaitLimits limits;
    BuildObserver observer;
    ToolPass pass({ db, digests, runner, limits, observer, { } }, inv);

    auto outcome = pass.run(tools, ctx);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failedToolName, "step2");
    EXPECT_TRUE(outcome.failure->notFound);
    EXPECT_EQ(outcome.failure->exitCode, 127);

    auto r1 = db.getRule("tool:step1:main");
    ASSERT_TRUE(r1.has_value());
    EXPECT_TRUE(r1->succeeded);

    auto r2 = db.getRule("tool:step2:main");
    ASSERT_TRUE(r2.has_value());
    EXPECT_FALSE(r2->succeeded);
}

TEST_F(ToolPassTest, timeoutAndCancellationSurfaceInOutcome)
{
    fs::create_directories(m_dir / "out");
    auto inFile = writeFile("out/main.idx", "idx");
    auto outFile = m_dir / "out" / "main.ind";

    StateDatabase db(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;
    FileStateTracker tracker(db, hasher);
    BuildGraph graph;
    FileDigests digests(graph, tracker, hasher);
    StubTool tool("makeindex", { { .path = inFile } }, { outFile }, InvokerKind::Makeindex);
    std::vector<ITool*> tools { &tool };
    auto inv = makeInv();
    auto ctx = makeEmptyCtx(inv);
    WaitLimits limits;
    BuildObserver observer;

    ProcessRunner timedOut = [](const ProcessLaunchSpec&) {
        return ProcessResult { .exitCode = -1, .timedOut = true };
    };
    auto o1 = ToolPass({ db, digests, timedOut, limits, observer, { } }, inv).run(tools, ctx);
    ASSERT_TRUE(o1.failure.has_value());
    EXPECT_TRUE(o1.failure->timedOut);

    ProcessRunner cancelled = [](const ProcessLaunchSpec&) {
        return ProcessResult { .exitCode = -1, .cancelled = true };
    };
    auto o2 = ToolPass({ db, digests, cancelled, limits, observer, { } }, inv).run(tools, ctx);
    ASSERT_TRUE(o2.failure.has_value());
    EXPECT_TRUE(o2.failure->cancelled);
}

TEST_F(ToolPassTest, snapshotErrorsPropagateDuringPlanningAndRecording)
{
    fs::create_directories(m_dir / "out");
    auto badInput = writeFile("out/bad.in", "data");
    auto goodInput = writeFile("out/good.in", "data");
    auto badOutput = writeFile("out/bad.out", "old");

    // 1. Error while digesting inputs during planning.
    {
        StateDatabase db(maketex::test_utils::makeMemoryDb());
        FailingHasher hasher(badInput);
        FileStateTracker tracker(db, hasher);
        BuildGraph graph;
        FileDigests digests(graph, tracker, hasher);
        StubTool tool("biber", { { .path = badInput } }, { m_dir / "out" / "x.bbl" });
        std::vector<ITool*> tools { &tool };
        auto inv = makeInv();
        auto ctx = makeEmptyCtx(inv);
        ProcessRunner runner = [](const ProcessLaunchSpec&) { return ProcessResult { .exitCode = 0 }; };
        WaitLimits limits;
        BuildObserver observer;

        auto outcome = ToolPass({ db, digests, runner, limits, observer, { } }, inv).run(tools, ctx);
        ASSERT_TRUE(outcome.snapshotError.has_value());
        EXPECT_EQ(*outcome.snapshotError, std::make_error_code(std::errc::permission_denied));
    }

    // 2. Error while hashing produced output after tool succeeds.
    {
        StateDatabase db(maketex::test_utils::makeMemoryDb());
        auto lateBadOut = m_dir / "out" / "late.out";
        FailingHasher hasher(lateBadOut);
        FileStateTracker tracker(db, hasher);
        BuildGraph graph;
        FileDigests digests(graph, tracker, hasher);
        StubTool tool("biber", { { .path = goodInput } }, { lateBadOut });
        std::vector<ITool*> tools { &tool };
        auto inv = makeInv();
        auto ctx = makeEmptyCtx(inv);
        ProcessRunner runner = [&](const ProcessLaunchSpec&) {
            std::ofstream(lateBadOut) << "written";
            return ProcessResult { .exitCode = 0 };
        };
        WaitLimits limits;
        BuildObserver observer;

        auto outcome = ToolPass({ db, digests, runner, limits, observer, { } }, inv).run(tools, ctx);
        ASSERT_TRUE(outcome.snapshotError.has_value());
        EXPECT_EQ(*outcome.snapshotError, std::make_error_code(std::errc::permission_denied));
        EXPECT_FALSE(outcome.ranAny);
        (void)badOutput;
    }
}

TEST_F(ToolPassTest, driverXdvipdfmxFailureMarksRuleFailedAndSkipsWhenXdvMissing)
{
    auto tex = writeFile("main.tex", "\\documentclass{article}\\begin{document}Hi\\end{document}");
    auto fls = m_dir / "main.fls";
    auto log = m_dir / "main.log";
    auto xdv = m_dir / "main.xdv";

    // Case 1: .xdv exists and xdvipdfmx fails -> build fails and rule marked failed.
    {
        StateDatabase db(maketex::test_utils::makeMemoryDb());
        Blake3Hasher hasher;
        ScriptedDriverEngine engine([&](const EngineInvocation& inv) {
            std::ofstream(fls) << "PWD " << m_dir.string() << "\nINPUT " << tex.string()
                               << "\nOUTPUT " << xdv.string() << "\n";
            std::ofstream(log) << "This is XeTeX\n";
            std::ofstream(xdv) << "xdv bytes";
            return RunResult<EngineInvocation> {
                .invocation = inv,
                .kind = InvokerKind::XeLaTeX,
                .exitCode = 0,
            };
        });
        int xdvCalls = 0;
        BuildDriver::Config cfg {
            .xelatexXdvSplit = true,
            .processRunner = [&](const ProcessLaunchSpec& spec) {
                ++xdvCalls;
                EXPECT_EQ(spec.argv.front(), "xdvipdfmx");
                return ProcessResult { .exitCode = 1, .stderrOutput = "xdvipdfmx:fatal: bad xdv\n" };
            },
        };
        BuildDriver driver(engine, db, hasher,
            EngineInvocation { .texFile = tex, .outputDir = m_dir }, std::move(cfg));

        auto res = driver.run();
        EXPECT_FALSE(res.success);
        EXPECT_EQ(xdvCalls, 1);
        EXPECT_EQ(res.lastError, "xdvipdfmx exited with code 1");
        ASSERT_FALSE(res.diagnostics.empty());
        auto rule = db.getRule("xelatex:main");
        ASSERT_TRUE(rule.has_value());
        EXPECT_FALSE(rule->succeeded);
    }

    // Case 2: .xdv absent -> xdvipdfmx is not invoked and build succeeds.
    fs::remove(xdv);
    {
        StateDatabase db(maketex::test_utils::makeMemoryDb());
        Blake3Hasher hasher;
        ScriptedDriverEngine engine([&](const EngineInvocation& inv) {
            std::ofstream(fls) << "PWD " << m_dir.string() << "\nINPUT " << tex.string() << "\n";
            std::ofstream(log) << "This is XeTeX\n";
            return RunResult<EngineInvocation> {
                .invocation = inv,
                .kind = InvokerKind::XeLaTeX,
                .exitCode = 0,
            };
        });
        int xdvCalls = 0;
        BuildDriver::Config cfg {
            .xelatexXdvSplit = true,
            .processRunner = [&](const ProcessLaunchSpec&) {
                ++xdvCalls;
                return ProcessResult { .exitCode = 0 };
            },
        };
        BuildDriver driver(engine, db, hasher,
            EngineInvocation { .texFile = tex, .outputDir = m_dir }, std::move(cfg));

        auto res = driver.run();
        EXPECT_TRUE(res.success);
        EXPECT_EQ(xdvCalls, 0);
    }
}

TEST_F(ToolPassTest, driverDiagnosticPolicyFailsToolOnlyUpToDatePath)
{
    auto tex = writeFile("main.tex", "\\documentclass{article}\\begin{document}Hi\\end{document}");
    auto bib = writeFile("refs.bib", "@book{a, title={A}}");
    auto bcf = m_dir / "main.bcf";
    auto bbl = m_dir / "main.bbl";
    auto fls = m_dir / "main.fls";
    auto log = m_dir / "main.log";
    auto pdf = m_dir / "main.pdf";

    StateDatabase db(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;

    ScriptedDriverEngine engine([&](const EngineInvocation& inv) {
        std::ofstream(bcf) << "bcf v1";
        std::ofstream(fls) << "PWD " << m_dir.string() << "\nINPUT " << tex.string()
                           << "\nOUTPUT " << bcf.string() << "\nOUTPUT " << pdf.string() << "\n";
        std::ofstream(log) << "This is pdfTeX\n";
        std::ofstream(pdf) << "%PDF";
        return RunResult<EngineInvocation> {
            .invocation = inv,
            .kind = InvokerKind::PdfLaTeX,
            .exitCode = 0,
        };
    });

    StubTool tool("biber", { { .path = bcf }, { .path = bib } }, { bbl });

    // Initial cold build succeeds cleanly.
    {
        BuildDriver::Config cfg {
            .tools = { &tool },
            .processRunner = [&](const ProcessLaunchSpec&) {
                std::ofstream(bbl) << "bbl v1";
                return ProcessResult { .exitCode = 0 };
            },
        };
        BuildDriver driver(engine, db, hasher,
            EngineInvocation { .texFile = tex, .outputDir = m_dir }, std::move(cfg));
        auto res = driver.run();
        ASSERT_TRUE(res.success) << res.lastError;
        EXPECT_EQ(engine.calls(), 2); // cold + rerun after bbl appeared
    }

    // Edit .bib (engine is up to date!), biber writes identical bbl but emits a warning ->
    // diagnosticPolicy fails the tool-only up-to-date path without running the engine.
    writeFile("refs.bib", "@book{a, title={A2}}");
    {
        BuildDriver::Config cfg {
            .tools = { &tool },
            .diagnosticPolicy = DiagnosticPolicy { .warningsAsErrors = true },
            .processRunner = [&](const ProcessLaunchSpec&) {
                return ProcessResult {
                    .exitCode = 0,
                    .stdoutOutput = "WARN - I didn't find a database entry for 'missing'\n",
                };
            },
        };
        BuildDriver driver(engine, db, hasher,
            EngineInvocation { .texFile = tex, .outputDir = m_dir }, std::move(cfg));
        auto res = driver.run();
        EXPECT_FALSE(res.success);
        EXPECT_EQ(res.lastError, "1 warning treated as error");
        EXPECT_EQ(engine.calls(), 2); // no extra engine pass
    }
}
