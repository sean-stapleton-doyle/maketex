#include "parsers/AuxParser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using maketex::AuxParser;
using maketex::AuxRecord;
namespace fs = std::filesystem;

namespace {

class AuxParserTest : public ::testing::Test {
protected:
    fs::path m_dir;

    void SetUp() override
    {
        m_dir = fs::temp_directory_path() / "maketex_aux_test";
        fs::create_directories(m_dir);
    }

    void TearDown() override { fs::remove_all(m_dir); }

    fs::path writeAux(const std::string& name, const std::string& content)
    {
        fs::path p = m_dir / name;
        std::ofstream(p) << content;
        return p;
    }
};

} // namespace

TEST_F(AuxParserTest, emptyAux_emptyRecord)
{
    auto aux = writeAux("main.aux", "\\relax\n");

    auto parsed = AuxParser {}.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    EXPECT_TRUE(rec.citations.empty());
    EXPECT_TRUE(rec.bibDataFiles.empty());
    EXPECT_TRUE(rec.inputs.empty());
}

TEST_F(AuxParserTest, extractsSingleCitation)
{
    auto aux = writeAux("main.aux", "\\citation{smith2024}\n");

    auto parsed = AuxParser {}.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.citations.size(), 1u);
    EXPECT_EQ(rec.citations[0], "smith2024");
}

TEST_F(AuxParserTest, extractsCommaSeparatedCitations)
{
    // \cite{a,b,c} writes a single \citation{a,b,c} line
    auto aux = writeAux("main.aux", "\\citation{alpha,beta,gamma}\n");

    auto parsed = AuxParser {}.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.citations.size(), 3u);
    EXPECT_EQ(rec.citations[0], "alpha");
    EXPECT_EQ(rec.citations[1], "beta");
    EXPECT_EQ(rec.citations[2], "gamma");
}

TEST_F(AuxParserTest, deduplicatesCitations)
{
    auto aux = writeAux("main.aux",
        "\\citation{smith2024}\n"
        "\\citation{jones2023}\n"
        "\\citation{smith2024}\n");

    auto parsed = AuxParser {}.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.citations.size(), 2u);
}

TEST_F(AuxParserTest, extractsBibData)
{
    auto aux = writeAux("main.aux", "\\bibdata{references}\n");

    auto parsed = AuxParser {}.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.bibDataFiles.size(), 1u);
    EXPECT_EQ(rec.bibDataFiles[0], "references");
}

TEST_F(AuxParserTest, extractsMultipleBibDataFiles)
{
    auto aux = writeAux("main.aux", "\\bibdata{refs1,refs2,refs3}\n");

    auto parsed = AuxParser {}.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.bibDataFiles.size(), 3u);
    EXPECT_EQ(rec.bibDataFiles[0], "refs1");
    EXPECT_EQ(rec.bibDataFiles[1], "refs2");
    EXPECT_EQ(rec.bibDataFiles[2], "refs3");
}

TEST_F(AuxParserTest, extractsInput)
{
    auto aux = writeAux("main.aux", "\\@input{chapter1.aux}\n");

    auto parsed = AuxParser {}.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.inputs.size(), 1u);
    EXPECT_EQ(rec.inputs[0], fs::weakly_canonical(m_dir / "chapter1.aux"));
}

TEST_F(AuxParserTest, resolveInputRelativeToAuxDir)
{
    // \@input paths are relative to the directory containing the .aux file.
    auto aux = writeAux("main.aux", "\\@input{chapters/intro.aux}\n");

    auto parsed = AuxParser {}.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.inputs.size(), 1u);
    EXPECT_EQ(rec.inputs[0], fs::weakly_canonical(m_dir / "chapters" / "intro.aux"));
}

TEST_F(AuxParserTest, ignoresOtherAuxContent)
{
    auto aux = writeAux("main.aux",
        "\\relax\n"
        "\\@writefile{toc}{\\contentsline {section}{Introduction}{1}{}}\n"
        "\\newlabel{sec:intro}{{1}{1}{Introduction}{section.1}{}}\n"
        "\\gdef \\@abspage@last{5}\n");

    auto parsed = AuxParser {}.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    EXPECT_TRUE(rec.citations.empty());
    EXPECT_TRUE(rec.bibDataFiles.empty());
    EXPECT_TRUE(rec.inputs.empty());
}

TEST_F(AuxParserTest, missingFileReturnsError)
{
    auto parsed = AuxParser {}.parse(m_dir / "nonexistent.aux");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), std::errc::no_such_file_or_directory);
}
