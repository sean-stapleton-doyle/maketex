#include "maketex/parsers/FlsParser.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

using maketex::FlsParser;
namespace fs = std::filesystem;

namespace {

class FlsParserTest : public ::testing::Test {
protected:
    fs::path m_dir;

    void SetUp() override
    {
        m_dir = maketex::test_utils::uniqueTempDir("maketex_fls_test");
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

TEST_F(FlsParserTest, ignoredPrefixLongerThanPathDoesNotOverrun)
{
    // Regression for the C++14 std::mismatch overload, which read past the
    // path's end when the ignore prefix was longer than the path itself.
    // Such an input must simply be kept (not ignored), with no UB.
    fs::path longPrefix = m_dir / "texmf" / "tree" / "deep";
    auto fls = writeFls("main.fls",
        "PWD " + m_dir.string() + "\n"
                                  "INPUT /a\n"
                                  "INPUT main.tex\n");

    auto parsed = FlsParser { }.addIgnorePrefix(longPrefix).parse(fls);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    // Both inputs kept: /a is shorter than the prefix and must not match.
    ASSERT_EQ(rec.inputs.size(), 2u);
    EXPECT_EQ(rec.inputs[0], fs::path("/a"));
    EXPECT_EQ(rec.inputs[1].filename(), "main.tex");
}

TEST_F(FlsParserTest, missingFileReturnsError)
{
    auto parsed = FlsParser { }.parse(m_dir / "nonexistent.fls");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), std::errc::no_such_file_or_directory);
}

// ── overridePwd ───────────────────────────────────────────────────────────────

TEST_F(FlsParserTest, overridePwdRebasesRelativeEntries)
{
    auto fls = writeFls("main.fls",
        "PWD /old/checkout\n"
        "INPUT ./main.tex\n"
        "OUTPUT ./main.pdf\n");

    FlsParser parser;
    parser.overridePwd(m_dir);
    auto rec = parser.parse(fls);
    ASSERT_TRUE(rec.has_value());

    ASSERT_EQ(rec->inputs.size(), 1u);
    EXPECT_EQ(rec->inputs[0], fs::weakly_canonical(m_dir / "main.tex"));
    ASSERT_EQ(rec->outputs.size(), 1u);
    EXPECT_EQ(rec->outputs[0], fs::weakly_canonical(m_dir / "main.pdf"));
}

TEST_F(FlsParserTest, overridePwdRebasesAbsoluteEntriesUnderRecordedPwd)
{
    auto fls = writeFls("main.fls",
        "PWD /old/checkout\n"
        "INPUT /old/checkout/chapters/ch1.tex\n");

    FlsParser parser;
    parser.overridePwd(m_dir);
    auto rec = parser.parse(fls);
    ASSERT_TRUE(rec.has_value());

    ASSERT_EQ(rec->inputs.size(), 1u);
    EXPECT_EQ(rec->inputs[0],
        fs::weakly_canonical(m_dir / "chapters/ch1.tex"));
}

TEST_F(FlsParserTest, overridePwdLeavesSystemPathsAlone)
{
    auto fls = writeFls("main.fls",
        "PWD /old/checkout\n"
        "INPUT /usr/share/texmf/article.cls\n");

    FlsParser parser;
    parser.overridePwd(m_dir);
    auto rec = parser.parse(fls);
    ASSERT_TRUE(rec.has_value());

    ASSERT_EQ(rec->inputs.size(), 1u);
    EXPECT_EQ(rec->inputs[0], fs::path("/usr/share/texmf/article.cls"));
}

TEST_F(FlsParserTest, overridePwdMatchingRecordedPwdIsANoOp)
{
    auto fls = writeFls("main.fls",
        "PWD " + m_dir.string() + "\n"
                                  "INPUT ./main.tex\n");

    FlsParser parser;
    parser.overridePwd(m_dir);
    auto rec = parser.parse(fls);
    ASSERT_TRUE(rec.has_value());
    ASSERT_EQ(rec->inputs.size(), 1u);
    EXPECT_EQ(rec->inputs[0], fs::weakly_canonical(m_dir / "main.tex"));
}
