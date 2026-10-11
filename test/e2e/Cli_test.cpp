// The maketex command line as a user meets it: no arguments, the exit
// codes scripts rely on, and the machine-readable formats CI consumes.

#include "E2eSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>

using namespace maketex::e2e;
namespace fs = std::filesystem;

namespace {

class CliTest : public E2eTest { };

} // namespace

TEST_F(CliTest, bareMaketexBuildsTheOnlyDocument)
{
    auto dir = project("paper");
    write(dir / "paper.tex", kHello);
    write(dir / "notes.txt", "not TeX");

    auto first = m_cli->run({ }, dir);
    EXPECT_EQ(first.exitCode, 0) << first.err;
    EXPECT_TRUE(contains(first.out, "done in 1 pass")) << first.out;
    EXPECT_TRUE(fs::exists(dir / "paper.pdf"));
    EXPECT_TRUE(fs::exists(dir / ".maketex" / ".gitignore"));

    auto second = m_cli->run({ }, dir);
    EXPECT_EQ(second.exitCode, 0) << second.err;
    EXPECT_TRUE(contains(second.out, "up to date")) << second.out;
}

TEST_F(CliTest, extensionIsOptional)
{
    auto dir = project("p");
    write(dir / "thesis.tex", kHello);
    auto r = m_cli->run({ "thesis" }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_TRUE(fs::exists(dir / "thesis.pdf"));
}

TEST_F(CliTest, chapterWithTexRootBuildsTheRoot)
{
    auto dir = project("book");
    write(dir / "book.tex",
        "\\documentclass{book}\n\\begin{document}\n\\include{chapters/one}\n\\end{document}\n");
    write(dir / "chapters" / "one.tex", "% !TEX root = ../book.tex\n\\chapter{One}\nText.\n");

    auto r = m_cli->run({ "chapters/one.tex" }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_TRUE(fs::exists(dir / "book.pdf"));
    EXPECT_FALSE(fs::exists(dir / "chapters" / "one.pdf"));
}

TEST_F(CliTest, usageErrorsExitTwo)
{
    auto dir = project("two");
    write(dir / "a.tex", kHello);
    write(dir / "b.tex", kHello);

    auto ambiguous = m_cli->run({ }, dir);
    EXPECT_EQ(ambiguous.exitCode, 2);
    EXPECT_TRUE(contains(ambiguous.err, "a.tex, b.tex")) << ambiguous.err;

    auto empty = m_cli->run({ }, project("empty"));
    EXPECT_EQ(empty.exitCode, 2);
    EXPECT_TRUE(contains(empty.err, "no LaTeX document")) << empty.err;

    EXPECT_EQ(m_cli->run({ "a.tex", "--no-such-flag" }, dir).exitCode, 2);
    EXPECT_EQ(m_cli->run({ "missing.tex" }, dir).exitCode, 2);
}

TEST_F(CliTest, documentErrorExitsOne)
{
    auto dir = project("bad");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\undefinedmacro\n\\end{document}\n");
    auto r = m_cli->run({ }, dir);
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_TRUE(contains(r.err, "main.tex:3")) << r.err;
    EXPECT_TRUE(contains(r.err, "Undefined control sequence")) << r.err;
}

TEST_F(CliTest, missingEngineExitsThree)
{
    auto dir = project("noengine");
    write(dir / "main.tex", kHello);
    auto r = m_cli->run({ "--engine-program", "maketex-no-such-latex" }, dir);
    EXPECT_EQ(r.exitCode, 3);
    EXPECT_TRUE(contains(r.err, "not installed")) << r.err;
}

TEST_F(CliTest, jsonIsOneDocumentOnSuccessFailureAndEnvironmentError)
{
    auto dir = project("json");
    write(dir / "main.tex", kHello);

    auto ok = m_cli->run({ "--format=json" }, dir);
    ASSERT_EQ(ok.exitCode, 0) << ok.err;
    auto doc = nlohmann::json::parse(ok.out);
    EXPECT_EQ(doc["success"], true);
    EXPECT_EQ(doc["engine"], "pdflatex");
    EXPECT_EQ(doc["pdf"], (dir / "main.pdf").string());

    auto upToDate = nlohmann::json::parse(m_cli->run({ "--format=json" }, dir).out);
    EXPECT_EQ(upToDate["upToDate"], true);

    auto missing = m_cli->run({ "--format=json", "--engine-program", "maketex-no-such-latex" }, dir);
    EXPECT_EQ(missing.exitCode, 3);
    auto failed = nlohmann::json::parse(missing.out); // exactly one document
    EXPECT_EQ(failed["success"], false);
    EXPECT_TRUE(failed["pdf"].is_null());
    auto diagnostics = failed["diagnostics"];
    EXPECT_TRUE(std::ranges::any_of(diagnostics, [](const auto& d) { return d["category"] == "missing-tool"; }))
        << diagnostics.dump(2);
}

TEST_F(CliTest, githubAnnotationsAreRelativeToTheWorkspace)
{
    // CI runs from the checkout root; the document sits in a subdirectory
    // and the error in an \input file below it.
    auto root = project("workspace");
    write(root / "paper" / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\input{chapters/ch1}\n\\end{document}\n");
    write(root / "paper" / "chapters" / "ch1.tex", "Intro.\n\n\\undefinedmacro\n");

    auto r = m_cli->run({ "--format=github", "paper/main.tex" }, root);
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_TRUE(contains(r.out, "::error file=paper/chapters/ch1.tex,line=3")) << r.out;
}

TEST_F(CliTest, explicitOutputDirectoryKeepsThePdf)
{
    auto dir = project("out");
    write(dir / "main.tex", kHello);
    auto r = m_cli->run({ "-o", "build" }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_TRUE(fs::exists(dir / "build" / "main.pdf"));
    EXPECT_FALSE(fs::exists(dir / "main.pdf"));
    EXPECT_FALSE(fs::exists(dir / ".maketex"));
}

TEST_F(CliTest, configChoosesDocumentAndOutputDirectory)
{
    auto dir = project("configured");
    write(dir / "paper.tex", kHello);
    write(dir / "draft.tex", kHello);
    write(dir / "maketex.toml", "main = \"paper.tex\"\noutput-dir = \"out\"\n");

    auto r = m_cli->run({ }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.err;
    EXPECT_TRUE(fs::exists(dir / "out" / "paper.pdf"));
    EXPECT_FALSE(fs::exists(dir / "out" / "draft.pdf"));
}

TEST_F(CliTest, timeoutStopsALoopingDocument)
{
    auto dir = project("loop");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\def\\x{\\x}\\x\n\\end{document}\n");
    auto start = std::chrono::steady_clock::now();
    auto r = m_cli->run({ "--timeout", "2" }, dir);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_NE(r.exitCode, 0);
    EXPECT_TRUE(contains(r.err, "timed out")) << r.err;
    EXPECT_LT(elapsed, std::chrono::seconds(30));
}
