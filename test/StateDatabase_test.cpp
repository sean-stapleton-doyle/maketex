#include "maketex/StateDatabase.h"

#include <gtest/gtest.h>

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
