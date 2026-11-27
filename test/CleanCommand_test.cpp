#include "commands/CleanCommand.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using maketex::BuildFileSets;
using maketex::BuildOptions;
using maketex::cli::EffectiveOptions;
using maketex::cli::ICleanEnv;
using maketex::cli::runCleanCommand;

namespace {

class FakeCleanEnv : public ICleanEnv {
public:
    std::map<fs::path, BuildFileSets> scannedByOutDir;
    std::vector<fs::path> published;
    std::vector<std::string> dbJobs;
    bool throwOnReadDb = false;
    std::set<fs::path> failRemovePaths;

    std::vector<std::pair<fs::path, std::string>> forgottenJobs;
    std::vector<fs::path> removedPaths;
    std::vector<fs::path> cleanedEmptyDirs;
    std::ostringstream outStream;
    std::ostringstream errStream;

    std::optional<BuildFileSets> scanBuildFiles(const BuildOptions& build) override
    {
        auto it = scannedByOutDir.find(build.resolvedOutputDir());
        if (it == scannedByOutDir.end())
            return std::nullopt;
        return it->second;
    }

    std::vector<fs::path> publishedArtifactPaths(const BuildOptions&) override
    {
        return published;
    }

    std::vector<std::string> readDbJobs(const fs::path&) override
    {
        if (throwOnReadDb)
            throw std::runtime_error("file is not a database");
        return dbJobs;
    }

    void forgetDbJob(const fs::path& db, const std::string& jobName) override
    {
        forgottenJobs.emplace_back(db, jobName);
    }

    std::uintmax_t removeAll(const fs::path& path, std::error_code& ec) override
    {
        if (failRemovePaths.contains(path)) {
            ec = std::make_error_code(std::errc::permission_denied);
            return 0;
        }
        removedPaths.push_back(path);
        return fs::remove_all(path, ec);
    }

    void removeIfEmptyDir(const fs::path& dir) override
    {
        cleanedEmptyDirs.push_back(dir);
    }

    std::ostream& out() override { return outStream; }
    std::ostream& err() override { return errStream; }
};

class CleanCommandTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_clean_cmd_test"; }

    void SetUp() override
    {
        TempDirFixture::SetUp();
        m_texFile = writeFile("main.tex", "\\documentclass{article}\n");
    }

    EffectiveOptions makeOptions(bool all = false, bool dryRun = false) const
    {
        EffectiveOptions opts;
        opts.build.texFile = m_texFile;
        opts.extras.cleanAll = all;
        opts.extras.dryRun = dryRun;
        return opts;
    }

    fs::path m_texFile;
};

} // namespace

TEST_F(CleanCommandTest, emptyPlanPrintsNothingToClean)
{
    FakeCleanEnv env;
    int code = runCleanCommand(makeOptions(), env);

    EXPECT_EQ(code, 0);
    EXPECT_EQ(env.outStream.str(), "maketex: nothing to clean\n");
    EXPECT_TRUE(env.errStream.str().empty());
    EXPECT_TRUE(env.removedPaths.empty());
}

TEST_F(CleanCommandTest, dryRunListsFilesWithSingularAndPluralCountsWithoutDeleting)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    auto aux = writeFile(".maketex/main.aux", "x");

    FakeCleanEnv singularEnv;
    EXPECT_EQ(runCleanCommand(makeOptions(/*all=*/false, /*dryRun=*/true), singularEnv), 0);
    EXPECT_NE(singularEnv.outStream.str().find("would remove " + aux.string()), std::string::npos);
    EXPECT_NE(singularEnv.outStream.str().find("maketex: 1 file (dry run, nothing deleted)"),
        std::string::npos);
    EXPECT_TRUE(fs::exists(aux));

    writeFile(".maketex/main.log", "x");
    FakeCleanEnv pluralEnv;
    EXPECT_EQ(runCleanCommand(makeOptions(/*all=*/false, /*dryRun=*/true), pluralEnv), 0);
    EXPECT_NE(pluralEnv.outStream.str().find("maketex: 2 files (dry run, nothing deleted)"),
        std::string::npos);
    EXPECT_TRUE(pluralEnv.removedPaths.empty());
}

TEST_F(CleanCommandTest, sweepsLegacyLayoutWhenDefaultOutDirHasNoFls)
{
    // No .maketex/main.fls, but legacy main.fls + main.aux sit beside main.tex.
    auto legacyFls = writeFile("main.fls", "x");
    auto legacyAux = writeFile("main.aux", "x");
    auto publishedPdf = writeFile("main.pdf", "x");

    FakeCleanEnv env;
    env.published = { publishedPdf };

    int code = runCleanCommand(makeOptions(/*all=*/true, /*dryRun=*/false), env);

    EXPECT_EQ(code, 0);
    EXPECT_FALSE(fs::exists(legacyFls));
    EXPECT_FALSE(fs::exists(legacyAux));
    EXPECT_FALSE(fs::exists(publishedPdf));
    // Published PDF must be removed once, not duplicated by the legacy pass.
    EXPECT_EQ(std::ranges::count(env.removedPaths, publishedPdf), 1);
}

TEST_F(CleanCommandTest, sharedDatabaseForgetsJobInsteadOfDeletingDb)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    auto db = writeFile(".maketex/.maketex.db", "db");
    auto aux = writeFile(".maketex/main.aux", "x");

    // Dry run: preserves DB and does NOT call forgetDbJob.
    {
        FakeCleanEnv env;
        env.dbJobs = { "main", "other" };
        EXPECT_EQ(runCleanCommand(makeOptions(/*all=*/true, /*dryRun=*/true), env), 0);
        EXPECT_TRUE(env.forgottenJobs.empty());
        EXPECT_EQ(env.outStream.str().find(".maketex.db"), std::string::npos);
    }

    // Real run: forgets "main" in DB, leaves DB file intact, deletes main.aux.
    {
        FakeCleanEnv env;
        env.dbJobs = { "main", "other" };
        EXPECT_EQ(runCleanCommand(makeOptions(/*all=*/true, /*dryRun=*/false), env), 0);
        ASSERT_EQ(env.forgottenJobs.size(), 1u);
        EXPECT_EQ(env.forgottenJobs[0].first, db);
        EXPECT_EQ(env.forgottenJobs[0].second, "main");
        EXPECT_NE(env.outStream.str().find("forgot main in " + db.string()), std::string::npos);
        EXPECT_TRUE(fs::exists(db));
        EXPECT_FALSE(fs::exists(aux));
    }
}

TEST_F(CleanCommandTest, corruptDatabaseFallsBackToRemovingDbFile)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    auto db = writeFile(".maketex/.maketex.db", "not-sqlite");

    FakeCleanEnv env;
    env.throwOnReadDb = true;

    int code = runCleanCommand(makeOptions(/*all=*/true, /*dryRun=*/false), env);

    EXPECT_EQ(code, 0);
    EXPECT_TRUE(env.forgottenJobs.empty());
    EXPECT_FALSE(fs::exists(db));
}

TEST_F(CleanCommandTest, reportsSkippedOutsideAndPartialRemovalFailures)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    auto aux = writeFile(".maketex/main.aux", "x");
    auto log = writeFile(".maketex/main.log", "x");
    fs::path outside = m_dir / "escaped.aux";

    FakeCleanEnv env;
    env.scannedByOutDir[outDir] = BuildFileSets { .generated = { outside } };
    env.failRemovePaths.insert(aux);

    int code = runCleanCommand(makeOptions(), env);

    EXPECT_EQ(code, 1);
    EXPECT_NE(env.errStream.str().find("skipping " + outside.string()), std::string::npos);
    EXPECT_NE(env.errStream.str().find("cannot remove " + aux.string()), std::string::npos);
    EXPECT_TRUE(fs::exists(aux));
    EXPECT_FALSE(fs::exists(log));
    // Default output mode checks for empty output dir cleanup.
    ASSERT_EQ(env.cleanedEmptyDirs.size(), 1u);
    EXPECT_EQ(env.cleanedEmptyDirs[0], outDir);
}

TEST_F(CleanCommandTest, customOutputDirSkipsEmptyDirCleanup)
{
    fs::path customOut = m_dir / "out";
    fs::create_directories(customOut);
    writeFile("out/main.aux", "x");

    auto opts = makeOptions();
    opts.build.outputDir = customOut;

    FakeCleanEnv env;
    EXPECT_EQ(runCleanCommand(opts, env), 0);
    EXPECT_TRUE(env.cleanedEmptyDirs.empty());
}
