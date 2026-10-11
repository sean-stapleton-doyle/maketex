#include "maketex/parsers/AuxParser.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using maketex::AuxParser;
namespace fs = std::filesystem;

namespace {

class AuxParserTest : public ::testing::Test {
protected:
    fs::path m_dir;

    void SetUp() override
    {
        m_dir = maketex::test_utils::uniqueTempDir("maketex_aux_test");
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

    auto parsed = AuxParser { }.parse(aux);
    ASSERT_TRUE(parsed.has_value());

    EXPECT_TRUE(parsed.value().bibDataFiles.empty());
}

TEST_F(AuxParserTest, extractsBibData)
{
    auto aux = writeAux("main.aux", "\\bibdata{references}\n");

    auto parsed = AuxParser { }.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.bibDataFiles.size(), 1u);
    EXPECT_EQ(rec.bibDataFiles[0], "references");
}

TEST_F(AuxParserTest, extractsMultipleBibDataFiles)
{
    auto aux = writeAux("main.aux", "\\bibdata{refs1,refs2,refs3}\n");

    auto parsed = AuxParser { }.parse(aux);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.bibDataFiles.size(), 3u);
    EXPECT_EQ(rec.bibDataFiles[0], "refs1");
    EXPECT_EQ(rec.bibDataFiles[1], "refs2");
    EXPECT_EQ(rec.bibDataFiles[2], "refs3");
}

TEST_F(AuxParserTest, ignoresOtherAuxContent)
{
    auto aux = writeAux("main.aux",
        "\\relax\n"
        "\\citation{smith2024}\n"
        "\\@input{chapter1.aux}\n"
        "\\@writefile{toc}{\\contentsline {section}{Introduction}{1}{}}\n"
        "\\newlabel{sec:intro}{{1}{1}{Introduction}{section.1}{}}\n"
        "\\gdef \\@abspage@last{5}\n");

    auto parsed = AuxParser { }.parse(aux);
    ASSERT_TRUE(parsed.has_value());

    EXPECT_TRUE(parsed.value().bibDataFiles.empty());
}

TEST_F(AuxParserTest, missingFileReturnsError)
{
    auto parsed = AuxParser { }.parse(m_dir / "nonexistent.aux");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), std::errc::no_such_file_or_directory);
}

TEST_F(AuxParserTest, followsIncludedChildAux)
{
    // \include{chap} puts the chapter's \bibdata in chap.aux, reached only
    // through \@input in the main .aux.
    fs::create_directories(m_dir / "chapters");
    writeAux("chapters/chap.aux", "\\relax\n\\citation{knuth}\n\\bibdata{refs}\n");
    auto aux = writeAux("main.aux", "\\relax\n\\@input{chapters/chap.aux}\n\\bibstyle{plain}\n");

    auto rec = AuxParser { }.parse(aux);
    ASSERT_TRUE(rec.has_value());
    ASSERT_EQ(rec->bibDataFiles.size(), 1u);
    EXPECT_EQ(rec->bibDataFiles[0], "refs");
    ASSERT_EQ(rec->bibStyles.size(), 1u);
    EXPECT_EQ(rec->bibStyles[0], "plain");
    // Document order: the child's lines come where it is \@input.
    EXPECT_EQ(rec->bibtexSignature, "\\citation{knuth}\n\\bibdata{refs}\n\\bibstyle{plain}\n");
}

TEST_F(AuxParserTest, missingChildAuxIsSkipped)
{
    auto aux = writeAux("main.aux", "\\@input{absent.aux}\n\\bibdata{refs}\n");
    auto rec = AuxParser { }.parse(aux);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->bibDataFiles.size(), 1u);
}

TEST_F(AuxParserTest, inputCycleTerminates)
{
    writeAux("a.aux", "\\@input{main.aux}\n\\citation{x}\n");
    auto aux = writeAux("main.aux", "\\@input{a.aux}\n");
    auto rec = AuxParser { }.parse(aux);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->bibtexSignature, "\\citation{x}\n");
}

TEST_F(AuxParserTest, signatureIgnoresLabelsAndPageNumbers)
{
    // What changes between passes (labels, page counts) must not change the
    // bibtex signature, or bibtex would rerun on every pass.
    auto first = writeAux("one.aux",
        "\\relax\n\\citation{k}\n\\newlabel{s}{{1}{1}}\n\\bibdata{refs}\n\\gdef \\@abspage@last{1}\n");
    auto second = writeAux("two.aux",
        "\\relax\n\\citation{k}\n\\newlabel{s}{{1}{2}}\n\\bibdata{refs}\n\\bibcite{k}{1}\n\\gdef \\@abspage@last{2}\n");
    EXPECT_EQ(AuxParser { }.parse(first)->bibtexSignature,
        AuxParser { }.parse(second)->bibtexSignature);
}

TEST_F(AuxParserTest, everydayContentIsChecked)
{
    // What a plain, cross-referenced, hyperref'd, bibtex document writes:
    // all of it is watched by something else, so no extra pass.
    auto aux = writeAux("main.aux",
        "\\relax \n"
        "\\providecommand\\hyper@newdestlabel[2]{}\n"
        "\\providecommand\\HyField@AuxAddToFields[1]{}\n"
        "\\providecommand\\babel@aux[2]{}\n\\babel@aux{english}{}\n"
        "\\citation{knuth}\n\\bibstyle{plain}\n\\bibdata{refs}\n"
        "\\@writefile{toc}{\\contentsline {section}{\\numberline {1}Intro}{1}{}}\n"
        "\\newlabel{sec:intro}{{1}{1}{}{section.1}{}}\n"
        "\\gdef \\@abspage@last{1}\n");
    auto rec = AuxParser { }.parse(aux);
    ASSERT_TRUE(rec.has_value());
    EXPECT_FALSE(rec->hasUncheckedContent);
}

TEST_F(AuxParserTest, packageDataIsUnchecked)
{
    auto aux = writeAux("main.aux", "\\relax\n\\gdef\\totalfigures{3}\n");
    auto rec = AuxParser { }.parse(aux);
    ASSERT_TRUE(rec.has_value());
    EXPECT_TRUE(rec->hasUncheckedContent);
}
