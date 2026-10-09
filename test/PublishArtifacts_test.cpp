#include "maketex/Build.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using maketex::publishArtifacts;

namespace {

class PublishArtifactsTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_publish_test"; }

    void SetUp() override
    {
        TempDirFixture::SetUp();
        m_outDir = m_dir / ".maketex";
        fs::create_directories(m_outDir);
    }

    static std::string slurp(const fs::path& p)
    {
        std::ostringstream s;
        s << std::ifstream(p).rdbuf();
        return s.str();
    }

    fs::path m_outDir;
};

} // namespace

TEST_F(PublishArtifactsTest, copiesPdfToDestination)
{
    std::ofstream(m_outDir / "main.pdf") << "pdf-bytes";

    auto published = publishArtifacts(m_outDir, m_dir, "main");

    ASSERT_EQ(published.size(), 1u);
    EXPECT_EQ(published[0], m_dir / "main.pdf");
    EXPECT_EQ(slurp(m_dir / "main.pdf"), "pdf-bytes");
    // The source copy stays — it is the build state the next pass reads.
    EXPECT_TRUE(fs::exists(m_outDir / "main.pdf"));
}

TEST_F(PublishArtifactsTest, overwritesStaleDestination)
{
    std::ofstream(m_outDir / "main.pdf") << "new";
    std::ofstream(m_dir / "main.pdf") << "old";

    publishArtifacts(m_outDir, m_dir, "main");

    EXPECT_EQ(slurp(m_dir / "main.pdf"), "new");
}

TEST_F(PublishArtifactsTest, includesSynctexWhenPresent)
{
    std::ofstream(m_outDir / "main.pdf") << "p";
    std::ofstream(m_outDir / "main.synctex.gz") << "s";

    auto published = publishArtifacts(m_outDir, m_dir, "main");

    ASSERT_EQ(published.size(), 2u);
    EXPECT_TRUE(fs::exists(m_dir / "main.synctex.gz"));
}

TEST_F(PublishArtifactsTest, missingPdfPublishesNothing)
{
    EXPECT_TRUE(publishArtifacts(m_outDir, m_dir, "main").empty());
    EXPECT_FALSE(fs::exists(m_dir / "main.pdf"));
}

TEST_F(PublishArtifactsTest, sameDirectoryIsANoOp)
{
    std::ofstream(m_outDir / "main.pdf") << "p";
    EXPECT_TRUE(publishArtifacts(m_outDir, m_outDir, "main").empty());
}

TEST_F(PublishArtifactsTest, noTempFileLeftBehind)
{
    std::ofstream(m_outDir / "main.pdf") << "p";
    publishArtifacts(m_outDir, m_dir, "main");
    EXPECT_FALSE(fs::exists(m_dir / "main.pdf.maketex-tmp"));
}
