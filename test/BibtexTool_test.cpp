#include "maketex/tools/BibtexTool.h"

#include "maketex/EngineInvocation.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"
#include "maketex/tools/InputLocator.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <ranges>

using maketex::AuxParser;
using maketex::AuxRecord;
using maketex::BibtexTool;
using maketex::EngineInvocation;
using maketex::FlsRecord;
using maketex::LogRecord;
using maketex::ToolContext;
using maketex::ToolInvocation;
namespace fs = std::filesystem;

namespace {

class BibtexToolTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_bibtex_test"; }

    void SetUp() override
    {
        TempDirFixture::SetUp();
        fs::create_directories(m_dir / "out");
        m_inv = EngineInvocation { .texFile = m_dir / "main.tex", .outputDir = m_dir / "out" };
    }

    ToolContext context() const
    {
        auto aux = m_inv.jobFile(".aux");
        return ToolContext(
            m_inv, [] { return LogRecord { }; },
            [aux] { return AuxParser { }.parse(aux).value_or(AuxRecord { }); },
            [] { return FlsRecord { }; });
    }

    BibtexTool m_tool;
    EngineInvocation m_inv;
};

} // namespace

TEST_F(BibtexToolTest, nameIsBibtex)
{
    EXPECT_EQ(m_tool.name(), "bibtex");
}

TEST_F(BibtexToolTest, buildArgsUsesBareJobName)
{
    // bibtex is run from workingDir (=outputDir) so only the bare job name is
    // passed — absolute paths are rejected by TeX Live's openout_any=p policy.
    ToolInvocation inv { .workingDir = "/src/build", .jobName = "thesis", .outputDir = "/src/build" };
    EXPECT_EQ(m_tool.buildArgs(inv), (std::vector<std::string> { "bibtex", "thesis" }));
}

TEST_F(BibtexToolTest, appliesOnlyWithBibdata)
{
    writeFile("out/main.aux", "\\relax\n\\citation{k}\n");
    EXPECT_FALSE(m_tool.applies(context()));
    writeFile("out/main.aux", "\\relax\n\\citation{k}\n\\bibdata{refs}\n");
    EXPECT_TRUE(m_tool.applies(context()));
}

TEST_F(BibtexToolTest, inputsAreSignatureAndLocatedDatabases)
{
    writeFile("out/main.aux", "\\citation{k}\n\\bibdata{refs,absent}\n\\bibstyle{plain}\n");
    writeFile("refs.bib", "@book{k, title={K}}\n");

    auto inputs = m_tool.inputs(context());
    ASSERT_EQ(inputs.size(), 2u); // absent.bib and the system plain.bst are skipped
    EXPECT_TRUE(inputs[0].content.has_value());
    EXPECT_EQ(*inputs[0].content, "\\citation{k}\n\\bibdata{refs,absent}\n\\bibstyle{plain}\n");
    EXPECT_EQ(inputs[1].path, m_dir / "refs.bib");
    EXPECT_FALSE(inputs[1].content.has_value());
}

TEST_F(BibtexToolTest, outputIsTheBbl)
{
    EXPECT_EQ(m_tool.outputs(context()), (std::vector<fs::path> { m_dir / "out" / "main.bbl" }));
}

TEST(ToolSearchEnvTest, prependsDocumentDirToExistingValue)
{
    ::setenv("BIBINPUTS", "/home/me/bib:", 1);
    ::unsetenv("BSTINPUTS");
    auto env = maketex::toolSearchEnv("/doc");
    ::unsetenv("BIBINPUTS");

    auto value = [&](const std::string& name) {
        for (const auto& [k, v] : env)
            if (k == name)
                return v;
        return std::string { };
    };
    EXPECT_EQ(value("BIBINPUTS"), "/doc:/home/me/bib:");
    EXPECT_EQ(value("BSTINPUTS"), "/doc:");
}
