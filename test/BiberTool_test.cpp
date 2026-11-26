#include "maketex/ToolFactory.h"
#include "maketex/tools/BiberTool.h"
#include "maketex/tools/HashTriggeredTool.h"

#include "maketex/EngineInvocation.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>

using maketex::AuxRecord;
using maketex::BiberTool;
using maketex::createTool;
using maketex::EngineInvocation;
using maketex::FlsRecord;
using maketex::HashTriggeredTool;
using maketex::LogRecord;
using maketex::ToolContext;
using maketex::ToolInvocation;
using maketex::ToolKind;
using maketex::ToolWorkingDir;
namespace fs = std::filesystem;

namespace {

class BiberToolTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_biber_test"; }

    void SetUp() override
    {
        TempDirFixture::SetUp();
        fs::create_directories(m_dir / "paper" / ".maketex");
        m_inv = EngineInvocation {
            .texFile = m_dir / "paper" / "main.tex",
            .outputDir = m_dir / "paper" / ".maketex",
        };
    }

    BiberTool m_tool;
    EngineInvocation m_inv;
};

} // namespace

TEST_F(BiberToolTest, prefersSourceWorkingDirectoryAndPassesOutputDirs)
{
    EXPECT_EQ(m_tool.workingDirPreference(), ToolWorkingDir::Source);

    ToolInvocation separated {
        .workingDir = "/proj/paper",
        .jobName = "thesis",
        .outputDir = "/proj/paper/.maketex",
        .sourceDir = "/proj/paper",
    };
    EXPECT_EQ(m_tool.buildArgs(separated),
        (std::vector<std::string> {
            "biber",
            "--input-directory=/proj/paper/.maketex",
            "--output-directory=/proj/paper/.maketex",
            "thesis",
        }));

    ToolInvocation inPlace {
        .workingDir = "/proj/paper",
        .jobName = "thesis",
        .outputDir = "/proj/paper",
        .sourceDir = "/proj/paper",
    };
    EXPECT_EQ(m_tool.buildArgs(inPlace), (std::vector<std::string> { "biber", "thesis" }));
}

TEST_F(BiberToolTest, inputsTrackBcfAndParentRelativeBibFile)
{
    fs::create_directories(m_dir / "shared");
    writeFile("shared/refs.bib", "@book{k, title={K}}\n");
    writeFile("paper/.maketex/main.bcf",
        "<bcf:controlfile>\n"
        "  <bcf:datasource type=\"file\" datatype=\"bibtex\">../shared/refs.bib</bcf:datasource>\n"
        "</bcf:controlfile>\n");

    FlsRecord fls;
    fls.outputs.push_back(fs::weakly_canonical(m_inv.jobFile(".bcf")));
    ToolContext ctx(
        m_inv,
        [] { return LogRecord { }; },
        [] { return AuxRecord { }; },
        [fls] { return fls; });

    EXPECT_TRUE(m_tool.applies(ctx));
    auto inputs = m_tool.inputs(ctx);
    ASSERT_EQ(inputs.size(), 2u);
    EXPECT_EQ(inputs[0].path, m_inv.jobFile(".bcf"));
    EXPECT_EQ(fs::weakly_canonical(inputs[1].path), fs::weakly_canonical(m_dir / "shared/refs.bib"));
}

TEST(HashTriggeredToolPlaceholders, expandsJobOutdirAndSourcedir)
{
    HashTriggeredTool tool(HashTriggeredTool::Recipe {
        .name = "custom",
        .argv = { "mytool", "--out=$outdir", "--src=$sourcedir", "$job.in" },
        .triggerExtensions = { ".in" },
        .outputExtensions = { ".out" },
    });
    ToolInvocation inv {
        .workingDir = "/proj/.maketex",
        .jobName = "paper",
        .outputDir = "/proj/.maketex",
        .sourceDir = "/proj",
    };
    EXPECT_EQ(tool.buildArgs(inv),
        (std::vector<std::string> {
            "mytool",
            "--out=/proj/.maketex",
            "--src=/proj",
            "paper.in",
        }));
}

TEST(PythontexRecipe, passesSourceWorkingDir)
{
    auto tool = createTool(ToolKind::Pythontex);
    ToolInvocation inv {
        .workingDir = "/proj/.maketex",
        .jobName = "main",
        .outputDir = "/proj/.maketex",
        .sourceDir = "/proj",
    };
    EXPECT_EQ(tool->buildArgs(inv),
        (std::vector<std::string> { "pythontex", "--workingdir=/proj", "main" }));
}
