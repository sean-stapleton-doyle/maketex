#include "BuildDriver.h"

#include "Blake3Hasher.h"
#include "EngineInvocation.h"
#include "IEngine.h"
#include "ITool.h"
#include "InvokerKind.h"
#include "RunDecision.h"
#include "RunResult.h"
#include "StateDatabase.h"
#include "ToolContext.h"
#include "ToolFactory.h"
#include "ToolInvocation.h"
#include "ToolRunner.h"

#include <gtest/gtest.h>

#include <taskflow/taskflow.hpp>

#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using maketex::Blake3Hasher;
using maketex::BuildDriver;
using maketex::EngineInvocation;
using maketex::InvokerKind;
using maketex::RunDecision;
using maketex::RunResult;
using maketex::StateDatabase;
using maketex::ToolContext;
using maketex::ToolInvocation;

namespace {

// ── FakeTool ──────────────────────────────────────────────────────────────────

// Minimal ITool fake. buildArgs returns `true` so ToolRunner launches a no-op
// process that exits 0; the test counts invocations via buildArgs.
class FakeTool : public maketex::ITool {
public:
    std::string_view name() const override { return "fake-tool"; }
    maketex::InvokerKind invokerKind() const override { return InvokerKind::Bibtex; }
    RunDecision decide(const ToolContext&) override
    {
        return m_hasRun ? RunDecision::Done : RunDecision::Run;
    }
    void onSuccess(const ToolContext&) override { m_hasRun = true; }
    std::vector<std::string> buildArgs(const ToolInvocation&) const override
    {
        ++m_callCount;
        return { "true" };
    }

    int callCount() const { return m_callCount; }

private:
    mutable int m_callCount = 0;
    bool m_hasRun = false;
};

// ── FakeEngine ────────────────────────────────────────────────────────────────

struct FakeCall {
    int exitCode = 0;
    std::string stdoutOutput;
    // Optional side-effect: write these files before returning.
    std::vector<std::pair<fs::path, std::string>> writes;
};

class FakeEngine : public maketex::IEngine {
public:
    explicit FakeEngine(std::vector<FakeCall> calls)
        : m_calls(std::move(calls))
    {
    }

    std::future<RunResult<EngineInvocation>> run(const EngineInvocation& inv) override
    {
        if (m_index >= static_cast<int>(m_calls.size()))
            throw std::out_of_range("FakeEngine: no more calls configured");

        auto& call = m_calls[static_cast<size_t>(m_index++)];
        for (auto& [path, content] : call.writes) {
            fs::create_directories(path.parent_path());
            std::ofstream(path) << content;
        }
        auto invPtr = std::make_shared<const EngineInvocation>(inv);
        std::promise<RunResult<EngineInvocation>> p;
        p.set_value(RunResult<EngineInvocation> {
            .invocation = std::move(invPtr),
            .kind = invokerKind(),
            .exitCode = call.exitCode,
            .stdoutOutput = call.stdoutOutput,
        });
        return p.get_future();
    }

    std::string_view name() const override { return "fake"; }
    maketex::InvokerKind invokerKind() const override { return InvokerKind::PdfLatex; }
    bool handlesOwnLoop() const override { return false; }

    int callCount() const { return m_index; }

private:
    std::vector<FakeCall> m_calls;
    int m_index = 0;
};

// ── Fixture ───────────────────────────────────────────────────────────────────

std::unique_ptr<SQLite::Database> makeMemoryDb()
{
    return std::make_unique<SQLite::Database>(
        ":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
}

class BuildDriverTest : public ::testing::Test {
protected:
    fs::path m_dir;
    StateDatabase m_db { makeMemoryDb() };
    Blake3Hasher m_hasher;
    tf::Executor m_executor { 1 };

    void SetUp() override
    {
        m_dir = fs::temp_directory_path() / "maketex_driver_test";
        fs::create_directories(m_dir);
        std::ofstream(m_dir / "main.tex")
            << "\\documentclass{article}\\begin{document}Hello\\end{document}";
    }

    void TearDown() override { fs::remove_all(m_dir); }

    EngineInvocation makeInv() const
    {
        return EngineInvocation {
            .texFile = m_dir / "main.tex",
            .outputDir = m_dir,
            .auxDir = m_dir,
        };
    }
};

} // namespace

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(BuildDriverTest, singlePassConverges)
{
    FakeEngine engine({ FakeCall {
        .writes = {
            { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
            { m_dir / "main.log", "This is pdfTeX\n" },
        },
    } });

    BuildDriver driver(engine, m_db, m_hasher, makeInv());
    auto res = driver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 1);
    EXPECT_EQ(engine.callCount(), 1);
}

TEST_F(BuildDriverTest, latexFailureStops)
{
    FakeEngine engine({ FakeCall {
        .exitCode = 1,
        .writes = {
            { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
            { m_dir / "main.log", "" },
        },
    } });

    BuildDriver driver(engine, m_db, m_hasher, makeInv());
    auto res = driver.run();

    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.passes, 1);
    EXPECT_FALSE(res.lastError.empty());
}

TEST_F(BuildDriverTest, rerunSignalTrigersSecondPass)
{
    FakeEngine engine({
        FakeCall {
            .writes = {
                { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
                { m_dir / "main.log", "Rerun to get cross-references right\n" },
            },
        },
        FakeCall {
            .writes = {
                { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
                { m_dir / "main.log", "This is pdfTeX\n" },
            },
        },
    });

    BuildDriver driver(engine, m_db, m_hasher, makeInv());
    auto res = driver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 2);
}

TEST_F(BuildDriverTest, maxPassesExceededReturnsFailure)
{
    std::vector<FakeCall> calls;
    for (int i = 0; i < 4; ++i) {
        calls.push_back(FakeCall {
            .writes = {
                { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
                { m_dir / "main.log", "Rerun to get cross-references right\n" },
            },
        });
    }
    FakeEngine engine(std::move(calls));

    BuildDriver driver(engine, m_db, m_hasher, makeInv(), /*maxPasses=*/4);
    auto res = driver.run();

    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.passes, 4);
    EXPECT_FALSE(res.lastError.empty());
}

TEST_F(BuildDriverTest, changedGeneratedFileTrigersRerun)
{
    const fs::path auxFile = m_dir / "main.aux";
    auto flsEntry = "PWD " + m_dir.string() + "\n"
        + "INPUT " + auxFile.string() + "\n"
        + "OUTPUT " + auxFile.string() + "\n";

    FakeEngine engine({
        FakeCall {
            .writes = {
                { m_dir / "main.fls", flsEntry },
                { m_dir / "main.log", "" },
                { auxFile, "\\relax\n" },
            },
        },
        FakeCall {
            .writes = {
                { m_dir / "main.fls", flsEntry },
                { m_dir / "main.log", "" },
                { auxFile, "\\relax\\citation{foo}\n" },
            },
        },
        FakeCall {
            .writes = {
                { m_dir / "main.fls", flsEntry },
                { m_dir / "main.log", "" },
            },
        },
    });

    BuildDriver driver(engine, m_db, m_hasher, makeInv());
    auto res = driver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 3);
}

TEST_F(BuildDriverTest, missingFlsIsHandledGracefully)
{
    FakeEngine engine({ FakeCall {
        .writes = {
            { m_dir / "main.log", "This is pdfTeX\n" },
        },
    } });

    BuildDriver driver(engine, m_db, m_hasher, makeInv());
    auto res = driver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 1);
}

TEST_F(BuildDriverTest, secondInvocationWithNoChangesSkips)
{
    // First build: writes .fls, .log, and persists input hashes to the db.
    // Second build (same db, fresh BuildDriver): no inputs changed → skip
    // engine entirely, return success with passes=0.
    const fs::path texFile = m_dir / "main.tex";
    auto flsEntry = "PWD " + m_dir.string() + "\n"
        + "INPUT " + texFile.string() + "\n";

    FakeEngine firstEngine({ FakeCall {
        .writes = {
            { m_dir / "main.fls", flsEntry },
            { m_dir / "main.log", "This is pdfTeX\n" },
        },
    } });
    BuildDriver firstDriver(firstEngine, m_db, m_hasher, makeInv());
    auto firstRes = firstDriver.run();
    ASSERT_TRUE(firstRes.success);
    ASSERT_EQ(firstRes.passes, 1);

    // Second invocation with nothing touched on disk.
    FakeEngine secondEngine({});
    BuildDriver secondDriver(secondEngine, m_db, m_hasher, makeInv());
    auto secondRes = secondDriver.run();

    EXPECT_TRUE(secondRes.success);
    EXPECT_EQ(secondRes.passes, 0);
    EXPECT_EQ(secondEngine.callCount(), 0);
}

TEST_F(BuildDriverTest, secondInvocationRunsWhenSourceChanges)
{
    const fs::path texFile = m_dir / "main.tex";
    auto flsEntry = "PWD " + m_dir.string() + "\n"
        + "INPUT " + texFile.string() + "\n";

    FakeEngine firstEngine({ FakeCall {
        .writes = {
            { m_dir / "main.fls", flsEntry },
            { m_dir / "main.log", "This is pdfTeX\n" },
        },
    } });
    BuildDriver firstDriver(firstEngine, m_db, m_hasher, makeInv());
    ASSERT_TRUE(firstDriver.run().success);

    // Modify the source.
    std::ofstream(texFile)
        << "\\documentclass{article}\\begin{document}Changed\\end{document}";

    FakeEngine secondEngine({ FakeCall {
        .writes = {
            { m_dir / "main.fls", flsEntry },
            { m_dir / "main.log", "This is pdfTeX\n" },
        },
    } });
    BuildDriver secondDriver(secondEngine, m_db, m_hasher, makeInv());
    auto secondRes = secondDriver.run();

    EXPECT_TRUE(secondRes.success);
    EXPECT_EQ(secondRes.passes, 1);
    EXPECT_EQ(secondEngine.callCount(), 1);
}

TEST_F(BuildDriverTest, skipFailsWhenOutputDeleted)
{
    const fs::path texFile = m_dir / "main.tex";
    const fs::path pdfFile = m_dir / "main.pdf";
    auto flsEntry = "PWD " + m_dir.string() + "\n"
        + "INPUT " + texFile.string() + "\n"
        + "OUTPUT " + pdfFile.string() + "\n";

    FakeEngine firstEngine({ FakeCall {
        .writes = {
            { m_dir / "main.fls", flsEntry },
            { m_dir / "main.log", "This is pdfTeX\n" },
            { pdfFile, "%PDF-1.5\n" },
        },
    } });
    BuildDriver firstDriver(firstEngine, m_db, m_hasher, makeInv());
    ASSERT_TRUE(firstDriver.run().success);

    // Inputs unchanged, but the product was deleted — must rebuild.
    fs::remove(pdfFile);

    FakeEngine secondEngine({ FakeCall {
        .writes = {
            { m_dir / "main.fls", flsEntry },
            { m_dir / "main.log", "This is pdfTeX\n" },
            { pdfFile, "%PDF-1.5\n" },
        },
    } });
    BuildDriver secondDriver(secondEngine, m_db, m_hasher, makeInv());
    auto res = secondDriver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 1);
    EXPECT_TRUE(fs::exists(pdfFile));
}

TEST_F(BuildDriverTest, documentsInSameDirectoryKeepSeparateRuleState)
{
    // Two documents sharing one directory (and one state db) must not clobber
    // each other's incremental state: rule identity includes the job name.
    std::ofstream(m_dir / "other.tex") << "\\documentclass{article}";

    auto invFor = [&](const std::string& stem) {
        return EngineInvocation {
            .texFile = m_dir / (stem + ".tex"),
            .outputDir = m_dir,
            .auxDir = m_dir,
        };
    };
    auto callFor = [&](const std::string& stem) {
        return FakeCall { .writes = {
            { m_dir / (stem + ".fls"),
                "PWD " + m_dir.string() + "\n"
                    + "INPUT " + (m_dir / (stem + ".tex")).string() + "\n" },
            { m_dir / (stem + ".log"), "This is pdfTeX\n" },
        } };
    };

    FakeEngine engineA({ callFor("main") });
    ASSERT_TRUE(BuildDriver(engineA, m_db, m_hasher, invFor("main")).run().success);

    FakeEngine engineB({ callFor("other") });
    ASSERT_TRUE(BuildDriver(engineB, m_db, m_hasher, invFor("other")).run().success);

    // Rebuilding the first document with nothing changed must still skip,
    // even though the second document was built in between.
    FakeEngine engineA2({});
    auto res = BuildDriver(engineA2, m_db, m_hasher, invFor("main")).run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 0);
    EXPECT_EQ(engineA2.callCount(), 0);
}

TEST_F(BuildDriverTest, toolTriggerChangeDefeatsIncrementalSkip)
{
    // A .bcf-style trigger file changes between invocations; the engine's own
    // inputs are untouched. The skip path must notice the tool is stale.
    const fs::path texFile = m_dir / "main.tex";
    const fs::path bcfFile = m_dir / "main.bcf";
    auto flsEntry = "PWD " + m_dir.string() + "\n"
        + "INPUT " + texFile.string() + "\n";
    auto engineCall = [&] {
        return FakeCall { .writes = {
            { m_dir / "main.fls", flsEntry },
            { m_dir / "main.log", "This is pdfTeX\n" },
        } };
    };

    maketex::ToolFactory factory(m_hasher);
    maketex::ToolRunner toolRunner(m_executor);

    // First session: engine writes the .bcf, tool fires, build converges and
    // records the tool's input hash.
    {
        auto firstCall = engineCall();
        firstCall.writes.emplace_back(bcfFile, "bcf v1\n");
        FakeEngine engine({ firstCall, engineCall() });
        auto biber = factory.hashTriggered(
            "biber", ".bcf", { "true" }, InvokerKind::Biber, /*runOnce=*/true);
        BuildDriver driver(engine, m_db, m_hasher, makeInv(),
            BuildDriver::kDefaultMaxPasses, { biber.get() }, &toolRunner);
        ASSERT_TRUE(driver.run().success);
    }

    // Second session, nothing changed: tool's recorded input matches → skip.
    {
        FakeEngine engine({});
        auto biber = factory.hashTriggered(
            "biber", ".bcf", { "true" }, InvokerKind::Biber, /*runOnce=*/true);
        BuildDriver driver(engine, m_db, m_hasher, makeInv(),
            BuildDriver::kDefaultMaxPasses, { biber.get() }, &toolRunner);
        auto res = driver.run();
        EXPECT_TRUE(res.success);
        EXPECT_EQ(res.passes, 0);
    }

    // Third session: the trigger file changed (e.g. the .bib edit propagated
    // into the .bcf) — skip must fail and the tool must run again.
    std::ofstream(bcfFile) << "bcf v2\n";
    {
        FakeEngine engine({ engineCall(), engineCall() });
        auto biber = factory.hashTriggered(
            "biber", ".bcf", { "true" }, InvokerKind::Biber, /*runOnce=*/true);
        BuildDriver driver(engine, m_db, m_hasher, makeInv(),
            BuildDriver::kDefaultMaxPasses, { biber.get() }, &toolRunner);
        auto res = driver.run();
        EXPECT_TRUE(res.success);
        EXPECT_GE(res.passes, 1);
        EXPECT_GE(engine.callCount(), 1);
    }
}

TEST_F(BuildDriverTest, hashTriggeredToolFiresOnBcfChange)
{
    // biber-style: HashTriggeredTool fires when the .bcf hash changes.
    // FakeEngine writes a .bcf on pass 1; pass 2 converges (runOnce=true).
    const fs::path bcfFile = m_dir / "main.bcf";

    FakeEngine engine({
        FakeCall {
            .writes = {
                { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
                { m_dir / "main.log", "This is pdfTeX\n" },
                { bcfFile, "bcf content\n" },
            },
        },
        FakeCall {
            .writes = {
                { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
                { m_dir / "main.log", "This is pdfTeX\n" },
            },
        },
    });

    maketex::ToolFactory factory(m_hasher);
    auto biber = factory.hashTriggered(
        "biber", ".bcf", { "true" }, InvokerKind::Biber, /*runOnce=*/true);

    maketex::ToolRunner toolRunner(m_executor);
    BuildDriver driver(engine, m_db, m_hasher, makeInv(),
        BuildDriver::kDefaultMaxPasses, { biber.get() }, &toolRunner);
    auto res = driver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 2);
}

TEST_F(BuildDriverTest, bibtexTriggeredByAuxBibdata)
{
    const fs::path auxFile = m_dir / "main.aux";
    auto flsEntry = "PWD " + m_dir.string() + "\n"
        + "INPUT " + auxFile.string() + "\n"
        + "OUTPUT " + auxFile.string() + "\n";

    FakeEngine engine({
        FakeCall {
            .writes = {
                { m_dir / "main.fls", flsEntry },
                { m_dir / "main.log", "" },
                { auxFile, "\\bibdata{refs}\n\\citation{foo}\n" },
            },
        },
        FakeCall {
            .writes = {
                { m_dir / "main.fls", flsEntry },
                { m_dir / "main.log", "" },
            },
        },
    });

    FakeTool bibtexTool;
    maketex::ToolRunner toolRunner(m_executor);
    BuildDriver driver(engine, m_db, m_hasher, makeInv(),
        BuildDriver::kDefaultMaxPasses, { &bibtexTool }, &toolRunner);
    auto res = driver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(bibtexTool.callCount(), 1);
}
