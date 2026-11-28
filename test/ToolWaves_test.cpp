#include "maketex/driver/ToolWaves.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using maketex::driver::planToolWaves;
using maketex::driver::ToolFootprint;
using maketex::driver::toolsConflict;

namespace {

ToolFootprint makeTool(std::initializer_list<const char*> inputs,
    std::initializer_list<const char*> outputs,
    bool outputsUnknown = false)
{
    ToolFootprint fp { .outputsUnknown = outputsUnknown };
    for (const char* i : inputs)
        fp.inputKeys.insert(i);
    for (const char* o : outputs)
        fp.outputKeys.insert(o);
    return fp;
}

} // namespace

TEST(ToolWavesTest, emptyInputProducesNoWaves)
{
    std::vector<ToolFootprint> tools;
    EXPECT_TRUE(planToolWaves(tools).empty());
}

TEST(ToolWavesTest, disjointToolsJoinSingleWave)
{
    std::vector<ToolFootprint> tools {
        makeTool({ "/project/main.bcf" }, { "/project/main.bbl" }),
        makeTool({ "/project/main.idx" }, { "/project/main.ind" }),
        makeTool({ "/project/main.nlo" }, { "/project/main.nls" }),
    };
    auto waves = planToolWaves(tools);
    ASSERT_EQ(waves.size(), 1u);
    EXPECT_EQ(waves[0], (std::vector<std::size_t> { 0, 1, 2 }));
}

TEST(ToolWavesTest, sharedReadInputDoesNotConflict)
{
    auto a = makeTool({ "/project/main.aux" }, { "/project/main.bbl" });
    auto b = makeTool({ "/project/main.aux" }, { "/project/main.ind" });
    EXPECT_FALSE(toolsConflict(a, b));
}

TEST(ToolWavesTest, sharedOutputConflicts)
{
    auto a = makeTool({ "/project/a.in" }, { "/project/shared.out" });
    auto b = makeTool({ "/project/b.in" }, { "/project/shared.out" });
    EXPECT_TRUE(toolsConflict(a, b));
}

TEST(ToolWavesTest, outputToInputDependencySplitsWavesInEitherDirection)
{
    auto producer = makeTool({ "/project/main.bcf" }, { "/project/main.bbl" });
    auto consumer = makeTool({ "/project/main.bbl" }, { "/project/main.gls" });
    EXPECT_TRUE(toolsConflict(producer, consumer));
    EXPECT_TRUE(toolsConflict(consumer, producer));

    std::vector<ToolFootprint> tools { producer, consumer };
    auto waves = planToolWaves(tools);
    ASSERT_EQ(waves.size(), 2u);
    EXPECT_EQ(waves[0], (std::vector<std::size_t> { 0 }));
    EXPECT_EQ(waves[1], (std::vector<std::size_t> { 1 }));
}

TEST(ToolWavesTest, unknownOutputsPreventParallelExecution)
{
    auto known = makeTool({ "/project/main.bcf" }, { "/project/main.bbl" });
    auto unknown = makeTool({ "/project/main.pytxcode" }, { }, /*outputsUnknown=*/true);
    EXPECT_TRUE(toolsConflict(known, unknown));
    EXPECT_TRUE(toolsConflict(unknown, known));

    std::vector<ToolFootprint> tools { known, unknown, known };
    auto waves = planToolWaves(tools);
    ASSERT_EQ(waves.size(), 3u);
    EXPECT_EQ(waves[0], (std::vector<std::size_t> { 0 }));
    EXPECT_EQ(waves[1], (std::vector<std::size_t> { 1 }));
    EXPECT_EQ(waves[2], (std::vector<std::size_t> { 2 }));
}

TEST(ToolWavesTest, orderPreservedWhenThirdToolConflictsWithFirst)
{
    // A and B are independent (join wave 0), C depends on A (starts wave 1),
    // D is independent of C (joins wave 1).
    std::vector<ToolFootprint> tools {
        makeTool({ "/project/a.in" }, { "/project/a.out" }),
        makeTool({ "/project/b.in" }, { "/project/b.out" }),
        makeTool({ "/project/a.out" }, { "/project/c.out" }),
        makeTool({ "/project/d.in" }, { "/project/d.out" }),
    };
    auto waves = planToolWaves(tools);
    ASSERT_EQ(waves.size(), 2u);
    EXPECT_EQ(waves[0], (std::vector<std::size_t> { 0, 1 }));
    EXPECT_EQ(waves[1], (std::vector<std::size_t> { 2, 3 }));
}
