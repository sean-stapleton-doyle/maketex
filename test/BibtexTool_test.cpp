#include "maketex/tools/BibtexTool.h"

#include <gtest/gtest.h>

using maketex::BibtexTool;
using maketex::ToolInvocation;

namespace {

class BibtexToolTest : public ::testing::Test {
protected:
    BibtexTool m_tool;
};

} // namespace

TEST_F(BibtexToolTest, nameIsBibtex)
{
    EXPECT_EQ(m_tool.name(), "bibtex");
}

TEST_F(BibtexToolTest, buildArgsBasic)
{
    ToolInvocation inv {
        .workingDir = "/tmp/job",
        .jobName = "main",
        .outputDir = "/tmp/job",
    };
    auto args = m_tool.buildArgs(inv);

    // bibtex is run from workingDir (=outputDir) so only the bare job name is
    // passed — absolute paths are rejected by TeX Live's openout_any=p policy.
    ASSERT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0], "bibtex");
    EXPECT_EQ(args[1], "main");
}

TEST_F(BibtexToolTest, buildArgsUsesJobName)
{
    ToolInvocation inv {
        .workingDir = "/src/build",
        .jobName = "thesis",
        .outputDir = "/src/build",
    };
    auto args = m_tool.buildArgs(inv);

    ASSERT_EQ(args.size(), 2u);
    EXPECT_EQ(args[0], "bibtex");
    EXPECT_EQ(args[1], "thesis");
}
