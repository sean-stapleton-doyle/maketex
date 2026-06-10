#include "graph/BuildGraph.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>

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
    EXPECT_EQ(m_graph.rules().size(), 1u);
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

// ── topologicalRuleOrder ──────────────────────────────────────────────────────

TEST_F(BuildGraphTest, topological_emptyGraph)
{
    EXPECT_TRUE(m_graph.topologicalRuleOrder().empty());
}

TEST_F(BuildGraphTest, topological_singleRule)
{
    m_graph.getOrAddRule("latex");
    auto order = m_graph.topologicalRuleOrder();
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0]->name(), "latex");
}

TEST_F(BuildGraphTest, topological_independentRulesPreserveInsertionOrder)
{
    // No edges between them — insertion order must be preserved.
    m_graph.getOrAddRule("latex");
    m_graph.getOrAddRule("makeindex");
    auto order = m_graph.topologicalRuleOrder();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0]->name(), "latex");
    EXPECT_EQ(order[1]->name(), "makeindex");
}

TEST_F(BuildGraphTest, topological_producerBeforeConsumer)
{
    // bibtex produces .bbl, which latex2 consumes → bibtex must come first.
    auto& bbl = m_graph.getOrAddFile(m_dir / "main.bbl", FileRole::Generated);

    // Add latex2 first (insertion order) to verify topological sort overrides it.
    auto& latex2 = m_graph.getOrAddRule("latex2");
    latex2.addInput(&bbl);

    auto& bibtex = m_graph.getOrAddRule("bibtex");
    bibtex.addOutput(&bbl);

    auto order = m_graph.topologicalRuleOrder();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0]->name(), "bibtex");
    EXPECT_EQ(order[1]->name(), "latex2");
}

TEST_F(BuildGraphTest, topological_chainOfThreeRules)
{
    auto& mid = m_graph.getOrAddFile(m_dir / "mid.aux", FileRole::Generated);
    auto& out = m_graph.getOrAddFile(m_dir / "out.bbl", FileRole::Generated);

    auto& ruleC = m_graph.getOrAddRule("ruleC");
    ruleC.addInput(&out);

    auto& ruleB = m_graph.getOrAddRule("ruleB");
    ruleB.addInput(&mid);
    ruleB.addOutput(&out);

    auto& ruleA = m_graph.getOrAddRule("ruleA");
    ruleA.addOutput(&mid);

    auto order = m_graph.topologicalRuleOrder();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0]->name(), "ruleA");
    EXPECT_EQ(order[1]->name(), "ruleB");
    EXPECT_EQ(order[2]->name(), "ruleC");
}

TEST_F(BuildGraphTest, topological_cycleThrows)
{
    // latex produces .aux; bibtex consumes .aux and produces .bbl;
    // latex consumes .bbl → cycle.
    auto& aux = m_graph.getOrAddFile(m_dir / "main.aux", FileRole::Generated);
    auto& bbl = m_graph.getOrAddFile(m_dir / "main.bbl", FileRole::Generated);

    auto& latex = m_graph.getOrAddRule("latex");
    latex.addOutput(&aux);
    latex.addInput(&bbl);

    auto& bibtex = m_graph.getOrAddRule("bibtex");
    bibtex.addInput(&aux);
    bibtex.addOutput(&bbl);

    EXPECT_THROW(m_graph.topologicalRuleOrder(), std::runtime_error);
}

TEST_F(BuildGraphTest, topological_selfLoopDoesNotCycle)
{
    // The latex rule reads and writes .aux in the same run.
    // A self-loop (rule produces and consumes the same file) must not
    // count as a cycle — it is handled by the fixed-point loop.
    auto& aux = m_graph.getOrAddFile(m_dir / "main.aux", FileRole::Generated);

    auto& latex = m_graph.getOrAddRule("latex");
    latex.addOutput(&aux);
    latex.addInput(&aux);

    EXPECT_NO_THROW(m_graph.topologicalRuleOrder());
}
