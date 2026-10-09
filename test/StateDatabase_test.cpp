#include "maketex/StateDatabase.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace maketex;

namespace {

std::unique_ptr<SQLite::Database> makeMemoryDb()
{
    return std::make_unique<SQLite::Database>(
        ":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
}

Hash makeHash(uint8_t fill)
{
    Hash h;
    h.fill(fill);
    return h;
}

} // namespace

// ── File state ────────────────────────────────────────────────────────────────

TEST(StateDatabase, getFileState_missingReturnsNullopt)
{
    StateDatabase db(makeMemoryDb());
    EXPECT_EQ(db.getFileState("/no/such/file"), std::nullopt);
}

TEST(StateDatabase, upsertAndGetFileState)
{
    StateDatabase db(makeMemoryDb());
    FileState fs { .path = "/a/b.tex", .exists = true, .mtime = 1000, .size = 512, .hash = makeHash(0xAB) };
    db.upsertFileState(fs);

    auto result = db.getFileState("/a/b.tex");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->path, "/a/b.tex");
    EXPECT_TRUE(result->exists);
    EXPECT_EQ(result->mtime, 1000);
    EXPECT_EQ(result->size, 512);
    EXPECT_EQ(result->hash, makeHash(0xAB));
}

TEST(StateDatabase, upsertFileState_overwritesExisting)
{
    StateDatabase db(makeMemoryDb());
    FileState fs { .path = "/a/b.tex", .exists = true, .mtime = 1000, .size = 512, .hash = makeHash(0x01) };
    db.upsertFileState(fs);

    fs.mtime = 2000;
    fs.hash = makeHash(0x02);
    db.upsertFileState(fs);

    auto result = db.getFileState("/a/b.tex");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->exists);
    EXPECT_EQ(result->mtime, 2000);
    EXPECT_EQ(result->hash, makeHash(0x02));
}

TEST(StateDatabase, upsertAndGetAbsentFileState)
{
    StateDatabase db(makeMemoryDb());
    db.upsertFileState(FileState::absent("/a/missing.aux"));

    auto result = db.getFileState("/a/missing.aux");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->path, "/a/missing.aux");
    EXPECT_FALSE(result->exists);
}

TEST(StateDatabase, absentOverwritesPresent)
{
    StateDatabase db(makeMemoryDb());
    FileState present { .path = "/a/b.aux", .exists = true, .mtime = 1, .size = 1, .hash = makeHash(0x01) };
    db.upsertFileState(present);
    db.upsertFileState(FileState::absent("/a/b.aux"));

    auto result = db.getFileState("/a/b.aux");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->exists);
}

// ── Rule records ──────────────────────────────────────────────────────────────

TEST(StateDatabase, getRule_missingReturnsNullopt)
{
    StateDatabase db(makeMemoryDb());
    EXPECT_EQ(db.getRule("latex"), std::nullopt);
}

TEST(StateDatabase, upsertAndGetRule)
{
    StateDatabase db(makeMemoryDb());
    RuleRecord rule { .name = "latex", .command = "pdflatex -recorder", .ran_at = 999, .succeeded = true };
    db.upsertRule(rule);

    auto result = db.getRule("latex");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "latex");
    EXPECT_EQ(result->command, "pdflatex -recorder");
    EXPECT_EQ(result->ran_at, 999);
    EXPECT_TRUE(result->succeeded);
}

// ── Rule input snapshots ──────────────────────────────────────────────────────

TEST(StateDatabase, getRuleInputHash_missingReturnsNullopt)
{
    StateDatabase db(makeMemoryDb());
    EXPECT_EQ(db.getRuleInputHash("latex", "/a/b.tex"), std::nullopt);
}

TEST(StateDatabase, setAndGetRuleInputs)
{
    StateDatabase db(makeMemoryDb());
    db.upsertRule({ .name = "latex", .command = "pdflatex", .ran_at = 0, .succeeded = false });

    std::vector<std::pair<std::string, Hash>> inputs {
        { "/a/main.tex", makeHash(0x01) },
        { "/a/chapter.tex", makeHash(0x02) },
    };
    db.setRuleInputs("latex", inputs);

    auto stored = db.getRuleInputs("latex");
    ASSERT_EQ(stored.size(), 2u);

    EXPECT_EQ(db.getRuleInputHash("latex", "/a/main.tex"), makeHash(0x01));
    EXPECT_EQ(db.getRuleInputHash("latex", "/a/chapter.tex"), makeHash(0x02));
}

TEST(StateDatabase, setRuleInputs_replacesExisting)
{
    StateDatabase db(makeMemoryDb());
    db.upsertRule({ .name = "latex", .command = "pdflatex", .ran_at = 0, .succeeded = false });

    db.setRuleInputs("latex", { { "/a/main.tex", makeHash(0x01) } });
    db.setRuleInputs("latex", { { "/a/other.tex", makeHash(0x02) } });

    auto stored = db.getRuleInputs("latex");
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0].first, "/a/other.tex");
    EXPECT_EQ(db.getRuleInputHash("latex", "/a/main.tex"), std::nullopt);
}

// ── manifest / compatibility ──────────────────────────────────────────────────

TEST(StateDatabase, freshDatabaseIsNotReset)
{
    StateDatabase db(makeMemoryDb(),
        { .projectRoot = "/proj", .fingerprint = "engine=pdflatex" });
    EXPECT_FALSE(db.wasReset());
}

TEST(StateDatabase, matchingFingerprintKeepsState)
{
    auto dir = maketex::test_utils::uniqueTempDir("maketex_statedb_keep");
    std::filesystem::create_directories(dir);
    auto path = (dir / "state.db").string();
    maketex::StateDbConfig config { .fingerprint = "fp-a" };
    {
        StateDatabase db(std::make_unique<SQLite::Database>(path,
                             SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
            config);
        db.upsertRule({ .name = "r", .command = "c", .ran_at = 1, .succeeded = true });
    }
    StateDatabase db(std::make_unique<SQLite::Database>(path,
                         SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
        config);
    EXPECT_FALSE(db.wasReset());
    EXPECT_TRUE(db.getRule("r").has_value());
    std::filesystem::remove_all(dir);
}

TEST(StateDatabase, changedFingerprintWipesState)
{
    // A file-backed db in a temp dir so two StateDatabase instances can
    // open the same store in sequence.
    auto dir = maketex::test_utils::uniqueTempDir("maketex_statedb_fp");
    std::filesystem::create_directories(dir);
    auto path = (dir / "state.db").string();
    {
        StateDatabase db(std::make_unique<SQLite::Database>(path,
                             SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
            { .fingerprint = "fp-a" });
        db.upsertRule({ .name = "r", .command = "c", .ran_at = 1, .succeeded = true });
    }
    StateDatabase db(std::make_unique<SQLite::Database>(path,
                         SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
        { .fingerprint = "fp-b" });
    EXPECT_TRUE(db.wasReset());
    EXPECT_FALSE(db.getRule("r").has_value());
    std::filesystem::remove_all(dir);
}

TEST(StateDatabase, schemaV1DatabaseIsWiped)
{
    // Simulate a pre-manifest database: tables with rows but no meta table.
    auto raw = makeMemoryDb();
    raw->exec("CREATE TABLE rules (name TEXT PRIMARY KEY, command TEXT NOT NULL,"
              " ran_at INTEGER NOT NULL DEFAULT 0,"
              " succeeded INTEGER NOT NULL DEFAULT 0);"
              "INSERT INTO rules VALUES ('old', 'cmd', 1, 1);");

    StateDatabase db(std::move(raw));
    EXPECT_TRUE(db.wasReset());
    EXPECT_FALSE(db.getRule("old").has_value());
}

// ── path portability ──────────────────────────────────────────────────────────

TEST(StateDatabase, projectRootMakesFileStatePortable)
{
    auto dir = maketex::test_utils::uniqueTempDir("maketex_statedb_portable");
    std::filesystem::create_directories(dir);
    auto path = (dir / "state.db").string();
    {
        StateDatabase db(std::make_unique<SQLite::Database>(path,
                             SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
            { .projectRoot = "/checkout/a" });
        db.upsertFileState(FileState {
            .path = "/checkout/a/main.tex", .exists = true, .mtime = 7, .size = 3 });
    }
    // Same store, different absolute location: the relative key must hit.
    StateDatabase db(std::make_unique<SQLite::Database>(path,
                         SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
        { .projectRoot = "/checkout/b" });
    auto state = db.getFileState("/checkout/b/main.tex");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->mtime, 7);
    EXPECT_EQ(state->path, "/checkout/b/main.tex");
    std::filesystem::remove_all(dir);
}

TEST(StateDatabase, ruleInputsDecodeAgainstCurrentRoot)
{
    StateDatabase db(makeMemoryDb(), { .projectRoot = "/proj" });
    db.upsertRule({ .name = "latex", .command = "c", .ran_at = 1, .succeeded = true });
    db.setRuleInputs("latex",
        { { "/proj/main.tex", Hash { 1 } },
            { "/usr/texmf/article.cls", Hash { 2 } } });

    EXPECT_TRUE(db.getRuleInputHash("latex", "/proj/main.tex").has_value());

    auto inputs = db.getRuleInputs("latex");
    ASSERT_EQ(inputs.size(), 2u);
    std::vector<std::string> paths { inputs[0].first, inputs[1].first };
    EXPECT_NE(std::find(paths.begin(), paths.end(), "/proj/main.tex"), paths.end());
    EXPECT_NE(std::find(paths.begin(), paths.end(), "/usr/texmf/article.cls"),
        paths.end());
}
