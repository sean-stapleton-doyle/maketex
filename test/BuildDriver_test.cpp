#include "maketex/BuildDriver.h"

#include "maketex/Blake3Hasher.h"
#include "maketex/EngineInvocation.h"
#include "maketex/IEngine.h"
#include "maketex/ITool.h"
#include "maketex/InvokerKind.h"
#include "maketex/RunResult.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolContext.h"
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
using maketex::RunResult;
using maketex::StateDatabase;
using maketex::ToolContext;
using maketex::ToolInvocation;

namespace {

// ── FakeTool ──────────────────────────────────────────────────────────────────

// File-driven ITool fake: applies when the engine pass wrote `trigger`
// (an OUTPUT line in its .fls), depends on the trigger plus any extra
// inputs, and when run writes outputContent to `output` through sh.
class FakeTool : public maketex::ITool {
public:
    FakeTool(fs::path trigger, fs::path output, std::string outputContent = "out v1\n")
        : m_trigger(std::move(trigger))
        , m_output(std::move(output))
        , m_outputContent(std::move(outputContent))
    {
    }

    std::string_view name() const override { return "fake-tool"; }
    maketex::InvokerKind invokerKind() const override { return InvokerKind::Bibtex; }

    std::vector<std::string> buildArgs(const ToolInvocation&) const override
    {
        ++m_runCount;
        if (m_fails)
            return { "false" };
        return { "sh", "-c", "printf '%s' \"$0\" > \"$1\"", m_outputContent,
            m_output.string() };
    }

    bool applies(const ToolContext& ctx) const override { return ctx.wrote(m_trigger); }

    std::vector<maketex::ToolInput> inputs(const ToolContext&) const override
    {
        std::vector<maketex::ToolInput> inputs;
        if (fs::exists(m_trigger))
            inputs.push_back({ .path = m_trigger });
        for (const auto& extra : m_extraInputs)
            if (fs::exists(extra))
                inputs.push_back({ .path = extra });
        return inputs;
    }

    std::vector<fs::path> outputs(const ToolContext&) const override
    {
        return { m_output };
    }

    void addInput(fs::path path) { m_extraInputs.push_back(std::move(path)); }
    void setFails(bool fails) { m_fails = fails; }
    int runCount() const { return m_runCount; }

private:
    fs::path m_trigger;
    fs::path m_output;
    std::string m_outputContent;
    std::vector<fs::path> m_extraInputs;
    bool m_fails = false;
    mutable int m_runCount = 0;
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

    RunResult<EngineInvocation> run(const EngineInvocation& inv, const maketex::WaitLimits&) override
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

    // .fls content for a pass that read main.tex.
    std::string flsWithTex() const
    {
        return "PWD " + m_dir.string() + "\nINPUT " + (m_dir / "main.tex").string() + "\n";
    }

    // A pass that reads main.tex and writes main.bcf with bcfContent — the
    // trigger FakeTool keys on.
    FakeCall triggerPass(const std::string& bcfContent) const
    {
        auto bcf = m_dir / "main.bcf";
        return FakeCall { .writes = {
                              { m_dir / "main.fls", flsWithTex() + "OUTPUT " + bcf.string() + "\n" },
                              { m_dir / "main.log", "This is pdfTeX\n" },
                              { bcf, bcfContent },
                          } };
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
        // Pass 1 creates the .aux; a newly written file is not by itself a
        // change (the log asks for the rerun here).
        FakeCall {
            .writes = {
                { m_dir / "main.fls", flsEntry },
                { m_dir / "main.log", "Rerun to get cross-references right\n" },
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

// ── Tool scheduling ───────────────────────────────────────────────────────────

TEST_F(BuildDriverTest, toolRunsOnceThenBuildConverges)
{
    // Pass 1 writes the trigger; the tool turns it into a .bbl the engine
    // reads, so pass 2 runs. Pass 2 rewrites an identical trigger: the tool's
    // inputs match its recorded run, so it does not run again.
    FakeEngine engine({ triggerPass("bcf v1\n"), triggerPass("bcf v1\n") });
    FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
    BuildDriver driver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } });
    auto res = driver.run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(res.passes, 2);
    EXPECT_EQ(tool.runCount(), 1);
}

TEST_F(BuildDriverTest, toolNotRerunWhenOnlyTheSourceChanges)
{
    // A prose edit changes the .tex but not what the tool reads: the next
    // invocation needs one engine pass and no tool run.
    {
        FakeEngine engine({ triggerPass("bcf v1\n"), triggerPass("bcf v1\n") });
        FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
        ASSERT_TRUE(BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } })
                .run()
                .success);
    }
    std::ofstream(m_dir / "main.tex") << "\\documentclass{article}\\begin{document}More\\end{document}";

    FakeEngine engine({ triggerPass("bcf v1\n") });
    FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } }).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(res.passes, 1);
    EXPECT_EQ(tool.runCount(), 0);
}

TEST_F(BuildDriverTest, toolOnlyInputChangeWithSameOutputNeedsNoEnginePass)
{
    // A comment-only .bib edit: the engine never reads the .bib, the tool
    // reruns, and its .bbl comes out identical — no engine pass at all.
    const fs::path bib = m_dir / "refs.bib";
    std::ofstream(bib) << "@book{a, title={A}}\n";
    {
        FakeEngine engine({ triggerPass("bcf v1\n"), triggerPass("bcf v1\n") });
        FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
        tool.addInput(bib);
        ASSERT_TRUE(BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } })
                .run()
                .success);
    }
    std::ofstream(bib, std::ios::app) << "% a comment\n";

    FakeEngine engine({ });
    FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
    tool.addInput(bib);
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } }).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(tool.runCount(), 1);
    EXPECT_EQ(res.passes, 0);
    EXPECT_EQ(engine.callCount(), 0);
}

TEST_F(BuildDriverTest, toolOnlyInputChangeWithNewOutputRunsEngine)
{
    const fs::path bib = m_dir / "refs.bib";
    std::ofstream(bib) << "@book{a, title={A}}\n";
    {
        FakeEngine engine({ triggerPass("bcf v1\n"), triggerPass("bcf v1\n") });
        FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
        tool.addInput(bib);
        ASSERT_TRUE(BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } })
                .run()
                .success);
    }
    std::ofstream(bib, std::ios::app) << "@book{b, title={B}}\n";

    FakeEngine engine({ triggerPass("bcf v1\n") });
    FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl", "out v2\n");
    tool.addInput(bib);
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } }).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(tool.runCount(), 1);
    EXPECT_EQ(res.passes, 1);
}

TEST_F(BuildDriverTest, staleTriggerLeftOnDiskDoesNotFireTool)
{
    // A .bcf from an earlier version of the document (biblatex since
    // removed) must not keep running the tool: the pass did not write it.
    std::ofstream(m_dir / "main.bcf") << "left over\n";
    FakeEngine engine({ FakeCall { .writes = {
                                       { m_dir / "main.fls", flsWithTex() },
                                       { m_dir / "main.log", "This is pdfTeX\n" },
                                   } } });
    FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } }).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(tool.runCount(), 0);
    EXPECT_EQ(res.passes, 1);
}

TEST_F(BuildDriverTest, deletedToolOutputIsRegenerated)
{
    {
        FakeEngine engine({ triggerPass("bcf v1\n"), triggerPass("bcf v1\n") });
        FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
        ASSERT_TRUE(BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } })
                .run()
                .success);
    }
    fs::remove(m_dir / "main.bbl");

    FakeEngine engine({ triggerPass("bcf v1\n") });
    FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } }).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(tool.runCount(), 1);
    EXPECT_TRUE(fs::exists(m_dir / "main.bbl"));
}

TEST_F(BuildDriverTest, toolFailureLeavesTheBuildStale)
{
    {
        FakeEngine engine({ triggerPass("bcf v1\n") });
        FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
        tool.setFails(true);
        auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } }).run();
        ASSERT_FALSE(res.success);
        EXPECT_NE(res.lastError.find("fake-tool"), std::string::npos);
    }

    // Nothing changed on disk, but the last build failed: it must rebuild.
    FakeEngine engine({ triggerPass("bcf v1\n"), triggerPass("bcf v1\n") });
    FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } }).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_GE(engine.callCount(), 1);
    EXPECT_EQ(tool.runCount(), 1);
}

// ── Convergence ───────────────────────────────────────────────────────────────

TEST_F(BuildDriverTest, nonConvergedBuildIsNotUpToDateNextTime)
{
    auto rerunPass = [&] {
        return FakeCall { .writes = {
                              { m_dir / "main.fls", flsWithTex() },
                              { m_dir / "main.log", "Rerun to get cross-references right\n" },
                          } };
    };
    {
        FakeEngine engine({ rerunPass() });
        auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .maxPasses = 1 }).run();
        ASSERT_FALSE(res.success);
    }

    FakeEngine engine({ FakeCall { .writes = {
                                       { m_dir / "main.fls", flsWithTex() },
                                       { m_dir / "main.log", "This is pdfTeX\n" },
                                   } } });
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv()).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(engine.callCount(), 1);
}

TEST_F(BuildDriverTest, newlyWrittenAuxAloneDoesNotForceSecondPass)
{
    // Every cold build logs "No file main.aux." and writes one; with no
    // rerun request that is a one-pass build.
    const fs::path aux = m_dir / "main.aux";
    FakeEngine engine({ FakeCall { .writes = {
                                       { m_dir / "main.fls",
                                           flsWithTex() + "INPUT " + aux.string() + "\nOUTPUT "
                                               + aux.string() + "\n" },
                                       { m_dir / "main.log", "No file main.aux.\n" },
                                       { aux, "\\relax\n" },
                                   } } });
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv()).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(res.passes, 1);
}

TEST_F(BuildDriverTest, missingFileThatNowExistsForcesSecondPass)
{
    // \tableofcontents on a cold build: "No file main.toc." then the pass
    // writes it — the next pass reads it.
    const fs::path toc = m_dir / "main.toc";
    FakeEngine engine({
        FakeCall { .writes = {
                       { m_dir / "main.fls", flsWithTex() + "OUTPUT " + toc.string() + "\n" },
                       { m_dir / "main.log", "No file main.toc.\n" },
                       { toc, "\\contentsline\n" },
                   } },
        FakeCall { .writes = {
                       { m_dir / "main.fls", flsWithTex() + "INPUT " + toc.string() + "\nOUTPUT " + toc.string() + "\n" },
                       { m_dir / "main.log", "This is pdfTeX\n" },
                       { toc, "\\contentsline\n" },
                   } },
    });
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv()).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(res.passes, 2);
}

TEST_F(BuildDriverTest, writeOnlyOutputChangeDoesNotForceRerun)
{
    // The engine writes .idx but never reads it back; its content changing
    // between passes is makeindex's concern, not a reason for another pass.
    const fs::path idx = m_dir / "main.idx";
    auto fls = flsWithTex() + "OUTPUT " + idx.string() + "\n";
    FakeEngine engine({
        FakeCall { .writes = {
                       { m_dir / "main.fls", fls },
                       { m_dir / "main.log", "Rerun to get cross-references right\n" },
                       { idx, "\\indexentry{a}{1}\n" },
                   } },
        FakeCall { .writes = {
                       { m_dir / "main.fls", fls },
                       { m_dir / "main.log", "This is pdfTeX\n" },
                       { idx, "\\indexentry{a}{2}\n" },
                   } },
    });
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv()).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(res.passes, 2);
}

TEST_F(BuildDriverTest, sourceEditedDuringPassForcesAnotherPass)
{
    // The user saves main.tex while the engine runs: what was compiled is
    // not what is on disk, so the build must not stop there.
    FakeEngine engine({
        FakeCall { .writes = {
                       { m_dir / "main.fls", flsWithTex() },
                       { m_dir / "main.log", "This is pdfTeX\n" },
                       { m_dir / "main.tex", "\\documentclass{article}\\begin{document}Saved mid-build\\end{document}" },
                   } },
        FakeCall { .writes = {
                       { m_dir / "main.fls", flsWithTex() },
                       { m_dir / "main.log", "This is pdfTeX\n" },
                   } },
    });
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv()).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(res.passes, 2);
}

TEST_F(BuildDriverTest, scratchOutputDeletedByEngineDoesNotBlockSkip)
{
    // Packages like minted write and delete scratch files in the same run;
    // the .fls lists them as outputs but they are never needed afterwards.
    auto fls = flsWithTex() + "OUTPUT " + (m_dir / "main.pyg").string() + "\n";
    {
        FakeEngine engine({ FakeCall { .writes = {
                                           { m_dir / "main.fls", fls },
                                           { m_dir / "main.log", "This is pdfTeX\n" },
                                       } } });
        ASSERT_TRUE(BuildDriver(engine, m_db, m_hasher, makeInv()).run().success);
    }
    FakeEngine engine({ });
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv()).run();

    EXPECT_TRUE(res.success) << res.lastError;
    EXPECT_EQ(res.passes, 0);
}

TEST_F(BuildDriverTest, missingToolBinaryIsADiagnosticNotACrash)
{
    // On BasicTeX, biber/makeglossaries are often absent: the build must
    // fail with a clear "not installed" diagnostic, not an exception.
    class MissingTool : public FakeTool {
    public:
        using FakeTool::FakeTool;
        std::vector<std::string> buildArgs(const ToolInvocation&) const override
        {
            return { "maketex-no-such-tool-xyz" };
        }
    };
    FakeEngine engine({ triggerPass("bcf v1\n") });
    MissingTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .tools = { &tool } }).run();

    ASSERT_FALSE(res.success);
    EXPECT_NE(res.lastError.find("not installed"), std::string::npos) << res.lastError;
    ASSERT_EQ(res.diagnostics.size(), 1u);
    EXPECT_EQ(res.diagnostics[0].category, "missing-tool");
    EXPECT_EQ(res.diagnostics[0].missingFile, "maketex-no-such-tool-xyz");
}

TEST_F(BuildDriverTest, zeroMaxPassesIsAnError)
{
    FakeEngine engine({ });
    auto res = BuildDriver(engine, m_db, m_hasher, makeInv(), { .maxPasses = 0 }).run();
    EXPECT_FALSE(res.success);
    EXPECT_FALSE(res.lastError.empty());
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

    FakeEngine engine({ triggerPass("bcf v1\n"), triggerPass("bcf v1\n") });

    FakeTool tool(m_dir / "main.bcf", m_dir / "main.bbl");
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
