#include "parsers/LogParser.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using maketex::LogParser;
namespace fs = std::filesystem;

namespace {

class LogParserTest : public ::testing::Test {
protected:
    fs::path m_dir;

    void SetUp() override
    {
        m_dir = fs::temp_directory_path() / "maketex_log_test";
        fs::create_directories(m_dir);
    }

    void TearDown() override { fs::remove_all(m_dir); }

    fs::path writeLog(const std::string& name, const std::string& content)
    {
        fs::path p = m_dir / name;
        std::ofstream(p) << content;
        return p;
    }
};

} // namespace

TEST_F(LogParserTest, cleanLog_allFlagsFalse)
{
    auto log = writeLog("main.log",
        "This is pdfTeX, Version 3.141592653\n"
        "Output written on main.pdf (1 page, 12345 bytes).\n"
        "Transcript written on main.log.\n");

    auto parsed = LogParser {}.parse(log);
    ASSERT_TRUE(parsed.has_value());
    const auto& rec = parsed.value();

    EXPECT_FALSE(rec.rerunNeeded);
}

TEST_F(LogParserTest, labelsMayHaveChanged_setsRerunNeeded)
{
    auto log = writeLog("main.log",
        "LaTeX Warning: Label(s) may have changed. Rerun to get cross-references right.\n");

    auto parsed = LogParser {}.parse(log);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed.value().rerunNeeded);
}

TEST_F(LogParserTest, hyperrefOutlines_setsRerunNeeded)
{
    auto log = writeLog("main.log",
        "Package hyperref Warning: Rerun to get outlines right (rerunfilecheck).\n");

    auto parsed = LogParser {}.parse(log);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed.value().rerunNeeded);
}

TEST_F(LogParserTest, rerunfilecheck_setsRerunNeeded)
{
    auto log = writeLog("main.log",
        "Package rerunfilecheck Warning: File `main.out' has changed.\n"
        "Rerun to get outlines right.\n");

    auto parsed = LogParser {}.parse(log);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed.value().rerunNeeded);
}

TEST_F(LogParserTest, pleaseRerunLatex_setsRerunNeeded)
{
    // biblatex emits this after biber has run and latex needs one more pass.
    auto log = writeLog("main.log",
        "Package biblatex Warning: Please rerun LaTeX.\n");

    auto parsed = LogParser {}.parse(log);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed.value().rerunNeeded);
}

TEST_F(LogParserTest, warningLinesWithoutRerunSignalAreIgnored)
{
    // Undefined references/citations are surfaced as user diagnostics by
    // DiagnosticExtractor from stdout — they are not rerun triggers.
    auto log = writeLog("main.log",
        "LaTeX Warning: There were undefined references.\n"
        "LaTeX Warning: Citation `jones2023' on page 2 undefined on input line 10.\n");

    auto parsed = LogParser {}.parse(log);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed.value().rerunNeeded);
}

TEST_F(LogParserTest, missingFileReturnsError)
{
    auto parsed = LogParser {}.parse(m_dir / "nonexistent.log");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), std::errc::no_such_file_or_directory);
}
