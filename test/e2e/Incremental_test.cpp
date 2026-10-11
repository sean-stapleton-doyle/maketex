// Everyday edit-and-rebuild cycles through the real binary: each pins how
// much work maketex does, read from its own report.

#include "E2eSupport.h"

#include <thread>

using namespace maketex::e2e;
namespace fs = std::filesystem;

namespace {

// Two valid 1×1 PNGs (red, blue) — a figure that changes.
const std::string kRedPng { "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90\x77\x53\xde\x00\x00\x00\x0c\x49\x44\x41\x54\x78\x9c\x63\xf8\xcf\xc0\x00\x00\x03\x01\x01\x00\xc9\xfe\x92\xef\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82", 69 };
const std::string kBluePng { "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90\x77\x53\xde\x00\x00\x00\x0c\x49\x44\x41\x54\x78\x9c\x63\x60\x60\xf8\x0f\x00\x01\x03\x01\x00\x08\x89\xc2\xec\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82", 69 };

class IncrementalCliTest : public E2eTest {
protected:
    // Builds dir (must succeed) and returns the summary line.
    std::string build(const fs::path& dir, std::vector<std::string> args = { })
    {
        auto r = m_cli->run(args, dir);
        EXPECT_EQ(r.exitCode, 0) << r.out << r.err;
        return r.out;
    }
};

} // namespace

TEST_F(IncrementalCliTest, editingAFigureRebuilds)
{
    auto dir = project("figure");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\usepackage{graphicx}\n\\begin{document}\n"
        "\\includegraphics{fig}\n\\end{document}\n");
    write(dir / "fig.png", kRedPng);
    build(dir);
    EXPECT_TRUE(contains(build(dir), "up to date"));

    write(dir / "fig.png", kBluePng);
    EXPECT_TRUE(contains(build(dir), "done in 1 pass"));
}

TEST_F(IncrementalCliTest, newSectionInATocDocumentTakesTwoPasses)
{
    auto dir = project("toc");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\tableofcontents\n"
        "\\section{One}\nText.\n\\end{document}\n");
    build(dir);

    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\tableofcontents\n"
        "\\section{One}\nText.\n\\section{Two}\nMore.\n\\end{document}\n");
    EXPECT_TRUE(contains(build(dir), "done in 2 passes"));
}

TEST_F(IncrementalCliTest, proseEditInAnIncludedChapterIsOnePass)
{
    auto dir = project("chapters");
    write(dir / "main.tex",
        "\\documentclass{book}\n\\begin{document}\n\\include{chapters/one}\n\\end{document}\n");
    write(dir / "chapters" / "one.tex", "\\chapter{One}\nFirst draft.\n");
    build(dir);

    write(dir / "chapters" / "one.tex", "\\chapter{One}\nSecond draft.\n");
    EXPECT_TRUE(contains(build(dir), "done in 1 pass"));
}

TEST_F(IncrementalCliTest, newIndexEntryRerunsMakeindex)
{
    if (!maketex::integration::hasProgram("makeindex"))
        GTEST_SKIP() << "not installed: makeindex";
    auto dir = project("index");
    auto doc = [](const std::string& extra) {
        return "\\documentclass{article}\n\\usepackage{makeidx}\n\\makeindex\n"
               "\\begin{document}\nAlpha\\index{alpha}. "
            + extra + "\n\\printindex\n\\end{document}\n";
    };
    write(dir / "main.tex", doc(""));
    build(dir);

    write(dir / "main.tex", doc("Beta\\index{beta}."));
    build(dir);
    std::ifstream ind(dir / ".maketex" / "main.ind");
    std::string content((std::istreambuf_iterator<char>(ind)), { });
    EXPECT_TRUE(contains(content, "beta")) << content;
}

TEST_F(IncrementalCliTest, deletedPdfIsRepublishedWithoutRebuilding)
{
    auto dir = project("republish");
    write(dir / "main.tex", kHello);
    build(dir);
    fs::remove(dir / "main.pdf");

    EXPECT_TRUE(contains(build(dir), "up to date"));
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));
}

TEST_F(IncrementalCliTest, deletedStateDirectoryRebuildsOnce)
{
    auto dir = project("fresh");
    write(dir / "main.tex", kHello);
    build(dir);
    fs::remove_all(dir / ".maketex");

    EXPECT_TRUE(contains(build(dir), "done in 1 pass"));
    EXPECT_TRUE(contains(build(dir), "up to date"));
}

TEST_F(IncrementalCliTest, concurrentBuildsOfOneDocumentBothSucceed)
{
    // A watch session and a manual build: the per-document lock makes them
    // take turns instead of writing the same .aux and PDF at once.
    auto dir = project("concurrent");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\section{A}\\label{a}\nSee \\ref{a}.\n\\end{document}\n");
    CliResult first;
    CliResult second;
    std::thread a([&] { first = m_cli->run({ }, dir); });
    std::thread b([&] { second = m_cli->run({ }, dir); });
    a.join();
    b.join();

    EXPECT_EQ(first.exitCode, 0) << first.err;
    EXPECT_EQ(second.exitCode, 0) << second.err;
    EXPECT_TRUE(contains(build(dir), "up to date"));
}
