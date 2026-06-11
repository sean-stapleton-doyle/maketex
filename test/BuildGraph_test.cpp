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
