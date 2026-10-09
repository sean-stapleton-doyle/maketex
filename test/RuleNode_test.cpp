#include "maketex/graph/RuleNode.h"

#include "maketex/Blake3Hasher.h"
#include "maketex/graph/FileNode.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using maketex::Blake3Hasher;
using maketex::FileNode;
using maketex::FileRole;
using maketex::RuleNode;
using maketex::StateDatabase;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<SQLite::Database> makeMemoryDb()
{
    return std::make_unique<SQLite::Database>(
        ":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
}

class RuleNodeTest : public ::testing::Test {
protected:
    fs::path m_dir;
    StateDatabase m_db { makeMemoryDb() };
    Blake3Hasher m_hasher;
    // Owns all FileNodes so raw pointers remain valid for the test's lifetime.
    std::vector<std::shared_ptr<FileNode>> m_nodes;

    void SetUp() override
    {
        m_dir = maketex::test_utils::uniqueTempDir("maketex_rulenode_test");
        fs::create_directories(m_dir);
    }

    void TearDown() override { fs::remove_all(m_dir); }

    fs::path write(const std::string& name, const std::string& content)
    {
        fs::path p = m_dir / name;
        std::ofstream(p) << content;
        return p;
    }

    // Creates a FileNode, writes its file, refreshes its state, and stores it
    // in m_nodes so the raw pointer remains valid for the test's lifetime.
    FileNode* makeNode(const std::string& name,
        const std::string& content,
        FileRole role = FileRole::Generated)
    {
        auto p = write(name, content);
        maketex::FileStateTracker tracker(m_db, m_hasher);
        auto node = std::make_shared<FileNode>(p, role);
        node->refresh(tracker);
        m_nodes.push_back(node);
        return node.get();
    }
};

} // namespace

// ── Structure ─────────────────────────────────────────────────────────────────

TEST_F(RuleNodeTest, nameIsAccessible)
{
    RuleNode rule("latex");
    EXPECT_EQ(rule.name(), "latex");
}

TEST_F(RuleNodeTest, inputsAndOutputsEmptyInitially)
{
    RuleNode rule("latex");
    EXPECT_TRUE(rule.inputs().empty());
    EXPECT_TRUE(rule.outputs().empty());
}

TEST_F(RuleNodeTest, addInputAppendsToInputs)
{
    RuleNode rule("latex");
    FileNode node(m_dir / "main.tex", FileRole::Source);
    rule.addInput(&node);
    ASSERT_EQ(rule.inputs().size(), 1u);
    EXPECT_EQ(rule.inputs()[0], &node);
}

TEST_F(RuleNodeTest, addOutputAppendsToOutputs)
{
    RuleNode rule("latex");
    FileNode node(m_dir / "main.pdf", FileRole::Output);
    rule.addOutput(&node);
    ASSERT_EQ(rule.outputs().size(), 1u);
    EXPECT_EQ(rule.outputs()[0], &node);
}

TEST_F(RuleNodeTest, addInput_idempotent)
{
    RuleNode rule("latex");
    FileNode node(m_dir / "main.tex", FileRole::Source);
    rule.addInput(&node);
    rule.addInput(&node);
    EXPECT_EQ(rule.inputs().size(), 1u);
}

// ── isStale ───────────────────────────────────────────────────────────────────

TEST_F(RuleNodeTest, isStale_trueWhenNeverRun)
{
    RuleNode rule("latex");
    rule.addInput(makeNode("main.tex", "hello"));
    EXPECT_TRUE(rule.isStale(m_db));
}

TEST_F(RuleNodeTest, isStale_falseAfterSuccessfulRun)
{
    RuleNode rule("latex");
    rule.addInput(makeNode("main.tex", "hello"));
    rule.markRan(m_db, true, "pdflatex main.tex");
    EXPECT_FALSE(rule.isStale(m_db));
}

TEST_F(RuleNodeTest, isStale_trueAfterFailedRun)
{
    RuleNode rule("latex");
    rule.addInput(makeNode("main.tex", "hello"));
    rule.markRan(m_db, false, "pdflatex main.tex");
    EXPECT_TRUE(rule.isStale(m_db));
}

TEST_F(RuleNodeTest, isStale_trueWhenInputContentChanges)
{
    auto p = write("main.tex", "version1");
    maketex::FileStateTracker tracker(m_db, m_hasher);
    auto node = std::make_shared<FileNode>(p, FileRole::Source);
    node->refresh(tracker);
    m_nodes.push_back(node);

    RuleNode rule("latex");
    rule.addInput(node.get());
    rule.markRan(m_db, true, "pdflatex main.tex");

    std::ofstream(p) << "version2";
    node->refresh(tracker);

    EXPECT_TRUE(rule.isStale(m_db));
}

TEST_F(RuleNodeTest, isStale_trueWhenNewInputAdded)
{
    RuleNode rule("latex");
    rule.addInput(makeNode("main.tex", "hello"));
    rule.markRan(m_db, true, "pdflatex main.tex");
    rule.addInput(makeNode("figure.pdf", "pdfdata"));
    EXPECT_TRUE(rule.isStale(m_db));
}

TEST_F(RuleNodeTest, isStale_trueWhenInputDisappears)
{
    auto p = write("main.tex", "hello");
    maketex::FileStateTracker tracker(m_db, m_hasher);
    auto node = std::make_shared<FileNode>(p, FileRole::Source);
    node->refresh(tracker);
    m_nodes.push_back(node);

    RuleNode rule("latex");
    rule.addInput(node.get());
    rule.markRan(m_db, true, "pdflatex main.tex");

    fs::remove(p);
    node->refresh(tracker);

    EXPECT_TRUE(rule.isStale(m_db));
}

// ── markRan ───────────────────────────────────────────────────────────────────

TEST_F(RuleNodeTest, markRan_persistsInputHashes)
{
    auto* node = makeNode("main.tex", "hello");
    RuleNode rule("latex");
    rule.addInput(node);
    rule.markRan(m_db, true, "pdflatex main.tex");

    auto stored = m_db.getRuleInputHash("latex", node->path().string());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, node->state().hash);
}

TEST_F(RuleNodeTest, markRan_updatesRuleRecord)
{
    RuleNode rule("latex");
    rule.markRan(m_db, true, "pdflatex main.tex");

    auto rec = m_db.getRule("latex");
    ASSERT_TRUE(rec.has_value());
    EXPECT_TRUE(rec->succeeded);
    EXPECT_EQ(rec->command, "pdflatex main.tex");
    EXPECT_GT(rec->ran_at, 0);
}

TEST_F(RuleNodeTest, markRan_noInputs_isNotStaleAfterSuccess)
{
    RuleNode rule("makeindex");
    rule.markRan(m_db, true, "makeindex main.idx");
    EXPECT_FALSE(rule.isStale(m_db));
}
