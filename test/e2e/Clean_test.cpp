// `maketex clean` on real build outputs.

#include "E2eSupport.h"

using namespace maketex::e2e;
namespace fs = std::filesystem;

namespace {

class CleanTest : public E2eTest {
protected:
    fs::path built(const std::string& name)
    {
        auto dir = project(name);
        write(dir / "main.tex", kHello);
        EXPECT_EQ(m_cli->run({ }, dir).exitCode, 0);
        return dir;
    }
};

} // namespace

TEST_F(CleanTest, defaultKeepsThePdfAndState)
{
    auto dir = built("keep");
    auto r = m_cli->run({ "clean" }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_FALSE(fs::exists(dir / ".maketex" / "main.aux"));
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));
    EXPECT_TRUE(fs::exists(dir / ".maketex" / ".maketex.db"));
}

TEST_F(CleanTest, allRemovesEverythingItMade)
{
    auto dir = built("all");
    auto r = m_cli->run({ "clean", "--all" }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_FALSE(fs::exists(dir / "main.pdf"));
    EXPECT_FALSE(fs::exists(dir / "main.synctex.gz"));
    EXPECT_FALSE(fs::exists(dir / ".maketex")) << "lock, .gitignore, or db left behind";
    EXPECT_TRUE(fs::exists(dir / "main.tex"));
}

TEST_F(CleanTest, dryRunDeletesNothing)
{
    auto dir = built("dry");
    auto r = m_cli->run({ "clean", "--all", "--dry-run" }, dir);
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(contains(r.out, "would remove")) << r.out;
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));
    EXPECT_TRUE(fs::exists(dir / ".maketex" / "main.aux"));
}

TEST_F(CleanTest, allOnOneDocumentKeepsTheOthersState)
{
    auto dir = project("shared");
    write(dir / "a.tex", kHello);
    write(dir / "b.tex", kHello);
    ASSERT_EQ(m_cli->run({ "a.tex" }, dir).exitCode, 0);
    ASSERT_EQ(m_cli->run({ "b.tex" }, dir).exitCode, 0);

    auto r = m_cli->run({ "clean", "--all", "a.tex" }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_FALSE(fs::exists(dir / "a.pdf"));
    EXPECT_TRUE(fs::exists(dir / ".maketex" / ".maketex.db"));
    EXPECT_TRUE(contains(m_cli->run({ "b.tex" }, dir).out, "up to date"));
}

TEST_F(CleanTest, sourceDirectoryOutputKeepsHandWrittenFiles)
{
    // -o . builds beside the sources, where a .ist may be the user's own.
    auto dir = project("inplace");
    write(dir / "main.tex", kHello);
    ASSERT_EQ(m_cli->run({ "-o", "." }, dir).exitCode, 0);
    write(dir / "main.ist", "% my index style\n");

    auto r = m_cli->run({ "clean", "-o", "." }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_FALSE(fs::exists(dir / "main.aux"));
    EXPECT_TRUE(fs::exists(dir / "main.ist"));
}
