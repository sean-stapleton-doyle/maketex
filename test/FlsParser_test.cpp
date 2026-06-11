#include "maketex/parsers/FlsParser.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

using maketex::FlsParser;
using maketex::FlsRecord;
namespace fs = std::filesystem;

namespace {

class FlsParserTest : public ::testing::Test {
protected:
    fs::path m_dir;

    void SetUp() override
    {
        m_dir = fs::temp_directory_path() / "maketex_fls_test";
        fs::create_directories(m_dir);
    }

    void TearDown() override { fs::remove_all(m_dir); }

    fs::path writeFls(const std::string& name, const std::string& content)
    {
        fs::path p = m_dir / name;
        std::ofstream(p) << content;
        return p;
    }
};

} // namespace

TEST_F(FlsParserTest, parsesBasicRecord)
{
    auto fls = writeFls("main.fls",
        "PWD " + m_dir.string() + "\n"
                                  "INPUT main.tex\n"
                                  "OUTPUT main.pdf\n");

    auto parsed = FlsParser { }.parse(fls);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    EXPECT_EQ(rec.pwd, fs::weakly_canonical(m_dir));
    ASSERT_EQ(rec.inputs.size(), 1u);
    EXPECT_EQ(rec.inputs[0], fs::weakly_canonical(m_dir / "main.tex"));
    ASSERT_EQ(rec.outputs.size(), 1u);
    EXPECT_EQ(rec.outputs[0], fs::weakly_canonical(m_dir / "main.pdf"));
}

TEST_F(FlsParserTest, absolutePathInInputLine)
{
    fs::path absInput = m_dir / "subdir" / "style.sty";
    auto fls = writeFls("main.fls",
        "PWD " + m_dir.string() + "\n"
                                  "INPUT "
            + absInput.string() + "\n");

    auto parsed = FlsParser { }.parse(fls);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.inputs.size(), 1u);
    EXPECT_EQ(rec.inputs[0], fs::weakly_canonical(absInput));
}

TEST_F(FlsParserTest, deduplicatesRepeatedPaths)
{
    auto fls = writeFls("main.fls",
        "PWD " + m_dir.string() + "\n"
                                  "INPUT main.tex\n"
                                  "INPUT main.tex\n"
                                  "OUTPUT main.aux\n"
                                  "OUTPUT main.aux\n");

    auto parsed = FlsParser { }.parse(fls);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    EXPECT_EQ(rec.inputs.size(), 1u);
    EXPECT_EQ(rec.outputs.size(), 1u);
}

TEST_F(FlsParserTest, fileAppearsInBothInputsAndOutputs)
{
    // .aux is written then re-read in the same run — the canonical rerun trigger.
    auto fls = writeFls("main.fls",
        "PWD " + m_dir.string() + "\n"
                                  "OUTPUT main.aux\n"
                                  "INPUT main.aux\n");

    auto parsed = FlsParser { }.parse(fls);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    auto auxPath = fs::weakly_canonical(m_dir / "main.aux");
    EXPECT_EQ(std::count(rec.inputs.begin(), rec.inputs.end(), auxPath), 1);
    EXPECT_EQ(std::count(rec.outputs.begin(), rec.outputs.end(), auxPath), 1);
}

TEST_F(FlsParserTest, filtersIgnoredPrefix)
{
    fs::path texmf = m_dir / "texmf";
    auto fls = writeFls("main.fls",
        "PWD " + m_dir.string() + "\n"
                                  "INPUT main.tex\n"
                                  "INPUT "
            + (texmf / "latex" / "article.cls").string() + "\n");

    auto parsed = FlsParser { }.addIgnorePrefix(texmf).parse(fls);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.inputs.size(), 1u);
    EXPECT_EQ(rec.inputs[0].filename(), "main.tex");
}

TEST_F(FlsParserTest, multipleIgnorePrefixes)
{
    fs::path texmf = m_dir / "texmf";
    fs::path fonts = m_dir / "fonts";
    auto fls = writeFls("main.fls",
        "PWD " + m_dir.string() + "\n"
                                  "INPUT main.tex\n"
                                  "INPUT "
            + (texmf / "article.cls").string() + "\n"
                                                 "INPUT "
            + (fonts / "cmr10.tfm").string() + "\n");

    auto parsed = FlsParser { }
                      .addIgnorePrefix(texmf)
                      .addIgnorePrefix(fonts)
                      .parse(fls);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    ASSERT_EQ(rec.inputs.size(), 1u);
    EXPECT_EQ(rec.inputs[0].filename(), "main.tex");
}

TEST_F(FlsParserTest, missingFileReturnsError)
{
    auto parsed = FlsParser { }.parse(m_dir / "nonexistent.fls");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), std::errc::no_such_file_or_directory);
}
