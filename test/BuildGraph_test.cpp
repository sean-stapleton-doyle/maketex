#include "maketex/graph/BuildGraph.h"

#include <gtest/gtest.h>

#include <filesystem>

using maketex::BuildGraph;
using maketex::FileRole;
namespace fs = std::filesystem;

namespace {

class BuildGraphTest : public ::testing::Test {
protected:
    BuildGraph m_graph;
    // Stable temp path prefix — files don't need to exist for graph structure tests.
    fs::path m_dir { "/tmp/maketex_graph_test" };
};

} // namespace

// ── Node creation and lookup ──────────────────────────────────────────────────

TEST_F(BuildGraphTest, getOrAddFile_createsNode)
{
    auto& node = m_graph.getOrAddFile(m_dir / "main.tex", FileRole::Source);
    EXPECT_EQ(node.role(), FileRole::Source);
}

TEST_F(BuildGraphTest, getOrAddFile_deduplicatesBySamePath)
{
    auto& a = m_graph.getOrAddFile(m_dir / "main.tex", FileRole::Source);
    auto& b = m_graph.getOrAddFile(m_dir / "main.tex", FileRole::Source);
    EXPECT_EQ(&a, &b);
    EXPECT_EQ(m_graph.files().size(), 1u);
}

TEST_F(BuildGraphTest, getOrAddFile_deduplicatesEquivalentPaths)
{
    // Same canonical path reached via different representations.
    auto& a = m_graph.getOrAddFile(m_dir / "sub" / ".." / "main.tex", FileRole::Source);
    auto& b = m_graph.getOrAddFile(m_dir / "main.tex", FileRole::Source);
    EXPECT_EQ(&a, &b);
}

TEST_F(BuildGraphTest, getOrAddRule_createsNode)
{
    auto& rule = m_graph.getOrAddRule("latex");
    EXPECT_EQ(rule.name(), "latex");
}

TEST_F(BuildGraphTest, getOrAddRule_deduplicatesByName)
{
    auto& a = m_graph.getOrAddRule("latex");
    auto& b = m_graph.getOrAddRule("latex");
    EXPECT_EQ(&a, &b);
}

TEST_F(BuildGraphTest, findFile_returnsNullForUnknownPath)
{
    EXPECT_EQ(m_graph.findFile(m_dir / "nonexistent.tex"), nullptr);
}

TEST_F(BuildGraphTest, findRule_returnsNullForUnknownName)
{
    EXPECT_EQ(m_graph.findRule("bibtex"), nullptr);
}

TEST_F(BuildGraphTest, findFile_returnsExistingNode)
{
    auto& added = m_graph.getOrAddFile(m_dir / "main.tex", FileRole::Source);
    EXPECT_EQ(m_graph.findFile(m_dir / "main.tex"), &added);
}

TEST_F(BuildGraphTest, findRule_returnsExistingNode)
{
    auto& added = m_graph.getOrAddRule("bibtex");
    EXPECT_EQ(m_graph.findRule("bibtex"), &added);
}

TEST_F(BuildGraphTest, constOverloadsFindAndListNodes)
{
    const BuildGraph& emptyConst = m_graph;
    EXPECT_EQ(emptyConst.findFile(m_dir / "main.tex"), nullptr);
    EXPECT_EQ(emptyConst.findRule("latex"), nullptr);
    EXPECT_TRUE(emptyConst.files().empty());

    auto& file = m_graph.getOrAddFile(m_dir / "main.tex", FileRole::Source);
    auto& aux = m_graph.getOrAddFile(m_dir / "main.aux", FileRole::Generated);
    auto& rule = m_graph.getOrAddRule("latex");

    const BuildGraph& constGraph = m_graph;
    EXPECT_EQ(constGraph.findFile(m_dir / "sub" / ".." / "main.tex"), &file);
    EXPECT_EQ(constGraph.findFile(m_dir / "missing.tex"), nullptr);
    EXPECT_EQ(constGraph.findRule("latex"), &rule);
    EXPECT_EQ(constGraph.findRule("missing"), nullptr);

    auto allFiles = constGraph.files();
    ASSERT_EQ(allFiles.size(), 2u);
    EXPECT_TRUE((allFiles[0] == &file && allFiles[1] == &aux)
        || (allFiles[0] == &aux && allFiles[1] == &file));
}
