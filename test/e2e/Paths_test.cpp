// Real-world project locations: spaces and non-ASCII characters in paths,
// and a project that moved since its last build.

#include "E2eSupport.h"

using namespace maketex::e2e;
namespace fs = std::filesystem;

namespace {

class PathsTest : public E2eTest {
protected:
    static constexpr const char* kWithChapter
        = "\\documentclass{article}\n\\begin{document}\n\\input{chapters/intro}\n\\end{document}\n";
};

} // namespace

TEST_F(PathsTest, directoryWithSpacesAndNonAscii)
{
    auto dir = project("My Thesis – Müller");
    write(dir / "main.tex", kWithChapter);
    write(dir / "chapters" / "intro.tex", "Introduction.\n");

    auto first = m_cli->run({ }, dir);
    EXPECT_EQ(first.exitCode, 0) << first.err;
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));
    auto second = m_cli->run({ }, dir);
    EXPECT_TRUE(contains(second.out, "up to date")) << second.out << second.err;
}

TEST_F(PathsTest, fileNamesWithSpaces)
{
    auto dir = project("spaces");
    write(dir / "My Paper.tex",
        "\\documentclass{article}\n\\begin{document}\n\\input{\"my chapter\"}\n\\end{document}\n");
    write(dir / "my chapter.tex", "A chapter.\n");

    auto first = m_cli->run({ "My Paper.tex" }, dir);
    EXPECT_EQ(first.exitCode, 0) << first.err;
    EXPECT_TRUE(fs::exists(dir / "My Paper.pdf"));

    // An edit to the spaced \input file is seen.
    write(dir / "my chapter.tex", "An edited chapter.\n");
    auto second = m_cli->run({ "My Paper.tex" }, dir);
    EXPECT_EQ(second.exitCode, 0) << second.err;
    EXPECT_FALSE(contains(second.out, "up to date")) << second.out;
}

TEST_F(PathsTest, movedProjectStaysUpToDate)
{
    auto dir = project("before");
    write(dir / "main.tex", kWithChapter);
    write(dir / "chapters" / "intro.tex", "Introduction.\n");
    ASSERT_EQ(m_cli->run({ }, dir).exitCode, 0);

    auto moved = m_sandbox / "after";
    fs::rename(dir, moved);
    auto r = m_cli->run({ }, moved);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "up to date")) << r.out;
}

TEST_F(PathsTest, copiedProjectStaysUpToDate)
{
    // A restored CI cache: every file has a new mtime, the content is the same.
    auto dir = project("original");
    write(dir / "main.tex", kWithChapter);
    write(dir / "chapters" / "intro.tex", "Introduction.\n");
    ASSERT_EQ(m_cli->run({ }, dir).exitCode, 0);

    auto copy = m_sandbox / "restored";
    fs::copy(dir, copy, fs::copy_options::recursive);
    auto r = m_cli->run({ }, copy);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "up to date")) << r.out;
}
