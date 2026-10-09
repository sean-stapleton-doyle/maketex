#include "maketex/BuildDriver.h"

#include "maketex/Blake3Hasher.h"
#include "maketex/EngineInvocation.h"
#include "maketex/IEngine.h"
#include "maketex/ITool.h"
#include "maketex/InvokerKind.h"
#include "maketex/RunDecision.h"
#include "maketex/RunResult.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolContext.h"
#include "maketex/ToolFactory.h"
#include "maketex/ToolInvocation.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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

// Minimal ITool fake. buildArgs returns `true` so the driver launches a no-op
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

    RunResult<EngineInvocation> run(const EngineInvocation& inv) override
    {
        if (m_index >= static_cast<int>(m_calls.size()))
            throw std::out_of_range("FakeEngine: no more calls configured");

        auto& call = m_calls[static_cast<size_t>(m_index++)];
        for (auto& [path, content] : call.writes) {
            fs::create_directories(path.parent_path());
            std::ofstream(path) << content;
        }
        return RunResult<EngineInvocation> {
            .invocation = inv,
            .kind = invokerKind(),
            .exitCode = call.exitCode,
            .stdoutOutput = call.stdoutOutput,
        };
    }

    std::string_view name() const override { return "fake"; }
    maketex::InvokerKind invokerKind() const override { return InvokerKind::PdfLaTeX; }

    int callCount() const { return m_index; }

private:
    std::vector<FakeCall> m_calls;
    int m_index = 0;
};

// ── Fixture ───────────────────────────────────────────────────────────────────

using maketex::test_utils::makeMemoryDb;

class BuildDriverTest : public ::testing::Test {
protected:
    fs::path m_dir;
    StateDatabase m_db { makeMemoryDb() };
    Blake3Hasher m_hasher;

    void SetUp() override
    {
        m_dir = maketex::test_utils::uniqueTempDir("maketex_driver_test");
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
    calls.reserve(4);
    for (int i = 0; i < 4; ++i) {
        calls.push_back(FakeCall {
            .writes = {
                { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
                { m_dir / "main.log", "Rerun to get cross-references right\n" },
            },
        });
    }
    FakeEngine engine(std::move(calls));

    BuildDriver driver(engine, m_db, m_hasher, makeInv(), { .maxPasses = 4 });
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
    FakeEngine secondEngine({ });
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
    FakeEngine engineA2({ });
    auto res = BuildDriver(engineA2, m_db, m_hasher, invFor("main")).run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 0);
    EXPECT_EQ(engineA2.callCount(), 0);
}

TEST_F(BuildDriverTest, secondInvocationFastPathAvoidsRehashing)
{
    // With file states persisted at the end of a build, the next invocation's
    // skip check satisfies itself via the (mtime, size) fast path — zero
    // hashFile calls.
    const fs::path texFile = m_dir / "main.tex";
    auto flsEntry = "PWD " + m_dir.string() + "\n"
        + "INPUT " + texFile.string() + "\n";

    maketex::test_utils::MockHasher hasher;

    FakeEngine firstEngine({ FakeCall {
        .writes = {
            { m_dir / "main.fls", flsEntry },
            { m_dir / "main.log", "This is pdfTeX\n" },
        },
    } });
    BuildDriver firstDriver(firstEngine, m_db, hasher, makeInv());
    ASSERT_TRUE(firstDriver.run().success);
    int callsAfterFirstBuild = hasher.callCount;
    ASSERT_GT(callsAfterFirstBuild, 0);

    FakeEngine secondEngine({ });
    BuildDriver secondDriver(secondEngine, m_db, hasher, makeInv());
    auto res = secondDriver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 0);
    EXPECT_EQ(hasher.callCount, callsAfterFirstBuild);
}

TEST_F(BuildDriverTest, ignoredPrefixFilesAreNotTracked)
{
    // Files under an ignore prefix (the TeX distribution) must not become
    // dependencies: editing one does not defeat the incremental skip.
    const fs::path texFile = m_dir / "main.tex";
    const fs::path texmf = m_dir / "texmf";
    fs::create_directories(texmf);
    std::ofstream(texmf / "article.cls") << "% class v1\n";

    auto flsEntry = "PWD " + m_dir.string() + "\n"
        + "INPUT " + texFile.string() + "\n"
        + "INPUT " + (texmf / "article.cls").string() + "\n";
    std::vector<fs::path> prefixes { texmf };

    FakeEngine firstEngine({ FakeCall {
        .writes = {
            { m_dir / "main.fls", flsEntry },
            { m_dir / "main.log", "This is pdfTeX\n" },
        },
    } });
    BuildDriver firstDriver(firstEngine, m_db, m_hasher, makeInv(),
        { .ignorePrefixes = prefixes });
    ASSERT_TRUE(firstDriver.run().success);

    std::ofstream(texmf / "article.cls") << "% class v2, updated distro\n";

    FakeEngine secondEngine({ });
    BuildDriver secondDriver(secondEngine, m_db, m_hasher, makeInv(),
        { .ignorePrefixes = prefixes });
    auto res = secondDriver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 0);
    EXPECT_EQ(secondEngine.callCount(), 0);
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

    // First session: engine writes the .bcf, tool fires, build converges and
    // records the tool's input hash.
    {
        auto firstCall = engineCall();
        firstCall.writes.emplace_back(bcfFile, "bcf v1\n");
        FakeEngine engine({ firstCall, engineCall() });
        auto biber = factory.hashTriggered(
            "biber", ".bcf", { "true" }, InvokerKind::Biber, /*runOnce=*/true);
        BuildDriver driver(engine, m_db, m_hasher, makeInv(),
            { .tools = { biber.get() } });
        ASSERT_TRUE(driver.run().success);
    }

    // Second session, nothing changed: tool's recorded input matches → skip.
    {
        FakeEngine engine({ });
        auto biber = factory.hashTriggered(
            "biber", ".bcf", { "true" }, InvokerKind::Biber, /*runOnce=*/true);
        BuildDriver driver(engine, m_db, m_hasher, makeInv(),
            { .tools = { biber.get() } });
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
            { .tools = { biber.get() } });
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
    BuildDriver driver(engine, m_db, m_hasher, makeInv(),
        { .tools = { biber.get() } });
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
    BuildDriver driver(engine, m_db, m_hasher, makeInv(),
        { .tools = { &bibtexTool } });
    auto res = driver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(bibtexTool.callCount(), 1);
}

TEST_F(BuildDriverTest, observerReceivesPassEvents)
{
    using maketex::BuildEvent;

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

    std::vector<BuildEvent> events;
    BuildDriver driver(engine, m_db, m_hasher, makeInv(),
        { .observer = [&](const BuildEvent& e) { events.push_back(e); } });
    auto res = driver.run();

    ASSERT_TRUE(res.success);
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0].kind, BuildEvent::Kind::PassStarted);
    EXPECT_EQ(events[0].pass, 1);
    EXPECT_EQ(events[1].kind, BuildEvent::Kind::PassFinished);
    EXPECT_EQ(events[1].pass, 1);
    EXPECT_TRUE(events[1].success);
    EXPECT_EQ(events[2].kind, BuildEvent::Kind::PassStarted);
    EXPECT_EQ(events[2].pass, 2);
    EXPECT_EQ(events[3].kind, BuildEvent::Kind::PassFinished);
    EXPECT_EQ(events[3].pass, 2);
}

TEST_F(BuildDriverTest, observerReceivesToolEvents)
{
    using maketex::BuildEvent;

    FakeEngine engine({
        FakeCall {
            .writes = {
                { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
                { m_dir / "main.log", "" },
            },
        },
        FakeCall {
            .writes = {
                { m_dir / "main.fls", "PWD " + m_dir.string() + "\n" },
                { m_dir / "main.log", "" },
            },
        },
    });

    FakeTool tool;
    std::vector<BuildEvent> events;
    BuildDriver driver(engine, m_db, m_hasher, makeInv(),
        { .tools = { &tool },
            .observer = [&](const BuildEvent& e) { events.push_back(e); } });
    auto res = driver.run();

    ASSERT_TRUE(res.success);
    auto started = std::find_if(events.begin(), events.end(), [](const auto& e) {
        return e.kind == BuildEvent::Kind::ToolStarted;
    });
    ASSERT_NE(started, events.end());
    EXPECT_EQ(started->toolName, "fake-tool");
    auto finished = std::find_if(events.begin(), events.end(), [](const auto& e) {
        return e.kind == BuildEvent::Kind::ToolFinished;
    });
    ASSERT_NE(finished, events.end());
    EXPECT_TRUE(finished->success);
}

TEST_F(BuildDriverTest, observerReceivesSkipEvent)
{
    using maketex::BuildEvent;

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

    FakeEngine secondEngine({ });
    std::vector<BuildEvent> events;
    BuildDriver secondDriver(secondEngine, m_db, m_hasher, makeInv(),
        { .observer = [&](const BuildEvent& e) { events.push_back(e); } });
    auto res = secondDriver.run();

    EXPECT_TRUE(res.success);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, BuildEvent::Kind::SkippedUpToDate);
}

// ── cache portability: the moved-checkout scenario ───────────────────────────

TEST_F(BuildDriverTest, incrementalSkipSurvivesDirectoryMove)
{
    // Build in a/, persist to a file-backed db inside it, rename a/ to b/
    // (what a restored CI cache at a different path looks like), and rebuild
    // — the second run must be a no-op.
    const fs::path a = m_dir / "a";
    fs::create_directories(a);
    std::ofstream(a / "main.tex") << "content";

    auto makeDb = [](const fs::path& root) {
        return StateDatabase(
            std::make_unique<SQLite::Database>((root / ".maketex.db").string(),
                SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
            { .projectRoot = root, .fingerprint = "fp" });
    };

    {
        FakeEngine engine({ FakeCall {
            .writes = {
                { a / "main.fls",
                    "PWD " + a.string() + "\nINPUT ./main.tex\nOUTPUT ./main.pdf\n" },
                { a / "main.log", "This is pdfTeX\n" },
                { a / "main.pdf", "pdf" },
            },
        } });
        auto db = makeDb(a);
        BuildDriver driver(engine, db, m_hasher,
            EngineInvocation { .texFile = a / "main.tex", .outputDir = a });
        auto res = driver.run();
        ASSERT_TRUE(res.success);
        ASSERT_EQ(res.passes, 1);
    }

    const fs::path b = m_dir / "b";
    fs::rename(a, b);

    FakeEngine engine({ }); // a rebuild attempt would throw out_of_range
    auto db = makeDb(b);
    BuildDriver driver(engine, db, m_hasher,
        EngineInvocation { .texFile = b / "main.tex", .outputDir = b });
    auto res = driver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 0); // skipped up to date — no engine call
    EXPECT_EQ(engine.callCount(), 0);
}

TEST_F(BuildDriverTest, movedCheckoutWithChangedSourceRebuilds)
{
    const fs::path a = m_dir / "a";
    fs::create_directories(a);
    std::ofstream(a / "main.tex") << "content";

    auto makeDb = [](const fs::path& root) {
        return StateDatabase(
            std::make_unique<SQLite::Database>((root / ".maketex.db").string(),
                SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
            { .projectRoot = root, .fingerprint = "fp" });
    };

    {
        FakeEngine engine({ FakeCall {
            .writes = {
                { a / "main.fls",
                    "PWD " + a.string() + "\nINPUT ./main.tex\nOUTPUT ./main.pdf\n" },
                { a / "main.log", "This is pdfTeX\n" },
                { a / "main.pdf", "pdf" },
            },
        } });
        auto db = makeDb(a);
        BuildDriver driver(engine, db, m_hasher,
            EngineInvocation { .texFile = a / "main.tex", .outputDir = a });
        ASSERT_TRUE(driver.run().success);
    }

    const fs::path b = m_dir / "b";
    fs::rename(a, b);
    std::ofstream(b / "main.tex") << "different content";

    FakeEngine engine({ FakeCall {
        .writes = {
            { b / "main.fls",
                "PWD " + b.string() + "\nINPUT ./main.tex\nOUTPUT ./main.pdf\n" },
            { b / "main.log", "This is pdfTeX\n" },
            { b / "main.pdf", "pdf2" },
        },
    } });
    auto db = makeDb(b);
    BuildDriver driver(engine, db, m_hasher,
        EngineInvocation { .texFile = b / "main.tex", .outputDir = b });
    auto res = driver.run();

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.passes, 1); // content change must defeat the skip
}
