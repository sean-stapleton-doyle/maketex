#include "maketex/DiagnosticExtractor.h"
#include "maketex/EngineInvocation.h"
#include "maketex/ToolFactory.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"
#include "maketex/tools/Bib2glsTool.h"
#include "maketex/tools/MakeglossariesTool.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

using maketex::AuxParser;
using maketex::AuxRecord;
using maketex::Bib2glsTool;
using maketex::createTool;
using maketex::Diagnostic;
using maketex::EngineInvocation;
using maketex::FlsRecord;
using maketex::InvokerKind;
using maketex::LogRecord;
using maketex::MakeglossariesTool;
using maketex::RunResult;
using maketex::ToolContext;
using maketex::ToolInvocation;
using maketex::ToolKind;
using maketex::ToolWorkingDir;
namespace fs = std::filesystem;

namespace {

class GlossariesAndNomenclTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_gloss_test"; }

    void SetUp() override
    {
        TempDirFixture::SetUp();
        fs::create_directories(m_dir / "doc" / ".maketex");
        m_inv = EngineInvocation {
            .texFile = m_dir / "doc" / "main.tex",
            .outputDir = m_dir / "doc" / ".maketex",
        };
    }

    EngineInvocation m_inv;
};

} // namespace

TEST_F(GlossariesAndNomenclTest, auxParserExtractsCustomGlossariesAndBib2glsResources)
{
    auto aux = writeFile("doc/.maketex/main.aux",
        "\\relax\n"
        "\\@istfilename{main.ist}\n"
        "\\@newglossary{main}{glg}{gls}{glo}\n"
        "\\@newglossary{symbols}{slg}{syi}{syg}\n"
        "\\glsxtr@resource{src={terms,../shared/extra},sort=en}{main-0}\n"
        "\\glsxtr@record{foo}{}{page}{glsnumberformat}{1}\n");

    auto parsed = AuxParser { }.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->hasUncheckedContent);

    ASSERT_EQ(parsed->glossaries.size(), 2u);
    EXPECT_EQ(parsed->glossaries[1].triggerExt, "syg");
    EXPECT_EQ(parsed->glossaries[1].outputExt, "syi");

    EXPECT_EQ(parsed->glsBibFiles, (std::vector<std::string> { "terms", "../shared/extra" }));
    EXPECT_EQ(parsed->glsOutputs, (std::vector<std::string> { "main-0" }));
    EXPECT_NE(parsed->bib2glsSignature.find("\\glsxtr@resource"), std::string::npos);
    EXPECT_NE(parsed->bib2glsSignature.find("\\glsxtr@record"), std::string::npos);
}

TEST_F(GlossariesAndNomenclTest, makeglossariesTracksCustomGlossaryExtensions)
{
    writeFile("doc/.maketex/main.aux",
        "\\relax\n\\@newglossary{symbols}{slg}{syi}{syg}\n");
    writeFile("doc/.maketex/main.syg", "\\glossaryentry{alpha}{1}\n");

    FlsRecord fls;
    fls.outputs.push_back(fs::weakly_canonical(m_inv.jobFile(".syg")));

    ToolContext ctx(
        m_inv,
        [] { return LogRecord { }; },
        [&] { return AuxParser { }.parse(m_inv.jobFile(".aux")).value_or(AuxRecord { }); },
        [fls] { return fls; });

    MakeglossariesTool tool;
    EXPECT_TRUE(tool.applies(ctx));

    auto inputs = tool.inputs(ctx);
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs[0].path, m_inv.jobFile(".syg"));

    auto outputs = tool.outputs(ctx);
    EXPECT_NE(std::ranges::find(outputs, m_inv.jobFile(".syi")), outputs.end());
}

TEST_F(GlossariesAndNomenclTest, bib2glsArgsInputsAndOutputs)
{
    fs::create_directories(m_dir / "shared");
    writeFile("shared/terms.bib", "@entry{a, name={A}, description={Desc}}\n");
    writeFile("doc/.maketex/main.aux",
        "\\relax\n"
        "\\glsxtr@resource{src=../shared/terms}{main-0}\n");

    ToolContext ctx(
        m_inv,
        [] { return LogRecord { }; },
        [&] { return AuxParser { }.parse(m_inv.jobFile(".aux")).value_or(AuxRecord { }); },
        [] { return FlsRecord { }; });

    Bib2glsTool tool;
    EXPECT_EQ(tool.workingDirPreference(), ToolWorkingDir::Source);
    EXPECT_TRUE(tool.applies(ctx));

    ToolInvocation inv {
        .workingDir = m_dir / "doc",
        .jobName = "main",
        .outputDir = m_dir / "doc" / ".maketex",
        .sourceDir = m_dir / "doc",
    };
    EXPECT_EQ(tool.buildArgs(inv),
        (std::vector<std::string> { "bib2gls", "-d", (m_dir / "doc" / ".maketex").string(), "main" }));

    auto inputs = tool.inputs(ctx);
    ASSERT_EQ(inputs.size(), 2u);
    EXPECT_TRUE(inputs[0].content.has_value());
    EXPECT_EQ(fs::weakly_canonical(inputs[1].path), fs::weakly_canonical(m_dir / "shared/terms.bib"));

    auto outputs = tool.outputs(ctx);
    EXPECT_EQ(outputs, (std::vector<fs::path> { m_inv.outputDir / "main-0.glstex" }));
}

TEST_F(GlossariesAndNomenclTest, nomenclRecipeMatchesStandardInvocation)
{
    auto tool = createTool(ToolKind::Nomencl);
    EXPECT_EQ(tool->name(), "nomencl");
    ToolInvocation inv {
        .workingDir = m_inv.outputDir,
        .jobName = "thesis",
        .outputDir = m_inv.outputDir,
        .sourceDir = m_dir / "doc",
    };
    EXPECT_EQ(tool->buildArgs(inv),
        (std::vector<std::string> {
            "makeindex", "thesis.nlo", "-s", "nomencl.ist", "-o", "thesis.nls" }));
}

TEST(ToolDiagnostics, extractsBib2glsAndCustomToolMessages)
{
    RunResult<ToolInvocation> bib2glsRes {
        .kind = InvokerKind::Bib2gls,
        .exitCode = 1,
        .stderrOutput = "Error: File 'terms.bib' not found\nWarning: Empty glossary\n",
    };
    auto bDiags = maketex::diagnostics::extract(bib2glsRes);
    ASSERT_EQ(bDiags.size(), 2u);
    EXPECT_EQ(bDiags[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(bDiags[1].severity, Diagnostic::Severity::Warning);

    RunResult<ToolInvocation> customRes {
        .kind = InvokerKind::Custom,
        .exitCode = 1,
        .stderrOutput = "error: bad input syntax\nwarning: deprecated macro\n",
    };
    auto cDiags = maketex::diagnostics::extract(customRes);
    ASSERT_EQ(cDiags.size(), 2u);
    EXPECT_EQ(cDiags[0].severity, Diagnostic::Severity::Error);
    EXPECT_EQ(cDiags[0].message, "bad input syntax");
    EXPECT_EQ(cDiags[1].severity, Diagnostic::Severity::Warning);
}
