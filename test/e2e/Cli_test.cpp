// The maketex command line as a user meets it: no arguments, the exit
// codes scripts rely on, and the machine-readable formats CI consumes.

#include "E2eSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

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

TEST_F(CliTest, customToolRunsOnTriggerAndConverges)
{
    auto dir = project("customtool");
    auto script = dir / "gen.sh";
    write(script,
        "#!/bin/sh\n"
        "printf 'Generated from: ' > \"$1.gen\"\n"
        "cat \"$1.raw\" >> \"$1.gen\"\n");
    fs::permissions(script, fs::perms::owner_all);

    write(dir / "main.tex",
        "\\documentclass{article}\n"
        "\\newwrite\\rawout\n"
        "\\begin{document}\n"
        "\\immediate\\openout\\rawout=\\jobname.raw\n"
        "\\immediate\\write\\rawout{hello-custom}\n"
        "\\immediate\\closeout\\rawout\n"
        "\\InputIfFileExists{\\jobname.gen}{}{pending}\n"
        "\\end{document}\n");
    write(dir / "maketex.toml",
        "[[tool]]\n"
        "name = \"gen\"\n"
        "command = [\""
            + script.string() + "\", \"$job\"]\n"
                                "triggers = [\".raw\"]\n"
                                "outputs = [\".gen\"]\n");

    auto cold = m_cli->run({ }, dir);
    EXPECT_EQ(cold.exitCode, 0) << cold.err;
    EXPECT_TRUE(contains(cold.out, "done in 2 passes")) << cold.out;
    EXPECT_TRUE(fs::exists(dir / ".maketex" / "main.gen"));
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));

    auto warm = m_cli->run({ }, dir);
    EXPECT_EQ(warm.exitCode, 0) << warm.err;
    EXPECT_TRUE(contains(warm.out, "up to date")) << warm.out;
}

TEST_F(CliTest, texinputsAndBibinputsResolveConfiguredDirectories)
{
    if (!maketex::integration::hasProgram("bibtex"))
        GTEST_SKIP() << "not installed: bibtex";
    auto dir = project("searchpaths");
    write(dir / "shared" / "nested" / "mycustom.sty",
        "\\NeedsTeXFormat{LaTeX2e}\\ProvidesPackage{mycustom}\n"
        "\\newcommand{\\myhello}{Hello from shared style}\n");
    write(dir / "bibs" / "external.bib",
        "@article{ext, author={Ada}, title={Notes}, journal={J}, year={1843}}\n");
    write(dir / "main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{mycustom}\n"
        "\\begin{document}\n"
        "\\myhello. See \\cite{ext}.\n"
        "\\bibliographystyle{plain}\n"
        "\\bibliography{external}\n"
        "\\end{document}\n");
    write(dir / "maketex.toml",
        "texinputs = [\"shared//\"]\n"
        "bibinputs = [\"bibs\"]\n");

    auto r = m_cli->run({ }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.out << r.err;
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));

    auto doc = m_cli->run({ "doctor", "main.tex" }, dir);
    EXPECT_EQ(doc.exitCode, 0) << doc.out << doc.err;
}

TEST_F(CliTest, diagnosticPolicyIgnoresAndFailsOnWarnings)
{
    auto dir = project("diagpolicy");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n"
        "See \\ref{missing-label}.\n"
        "\\end{document}\n");
    write(dir / "maketex.toml",
        "[diagnostics]\n"
        "fail-on = [\"undefined-reference\"]\n");

    auto first = m_cli->run({ }, dir);
    EXPECT_EQ(first.exitCode, 1) << first.out << first.err;
    EXPECT_TRUE(contains(first.err, "undefined-reference")) << first.err;

    // Must NOT report "up to date" on a subsequent run while the failing warning persists.
    auto second = m_cli->run({ }, dir);
    EXPECT_EQ(second.exitCode, 1) << second.out << second.err;

    // Ignoring the category lets the build pass quietly.
    write(dir / "maketex.toml",
        "[diagnostics]\n"
        "ignore = [\"undefined-reference\"]\n");
    auto ignored = m_cli->run({ }, dir);
    EXPECT_EQ(ignored.exitCode, 0) << ignored.out << ignored.err;
    EXPECT_FALSE(contains(ignored.err, "missing-label")) << ignored.err;

    // --warnings-as-errors fails on the warning when not ignored.
    auto werror = m_cli->run({ "--no-config", "--warnings-as-errors" }, dir);
    EXPECT_EQ(werror.exitCode, 1) << werror.out << werror.err;
}

TEST_F(CliTest, contentFiltersAllowVolatileAuxToConverge)
{
    auto dir = project("filter");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\makeatletter\n\\providecommand\\runs{0}\n"
        "\\AtEndDocument{\\immediate\\write\\@auxout{\\gdef\\string\\runs{\\the\\numexpr\\runs+1\\relax}}}\n"
        "\\makeatother\n\\begin{document}\nRun \\runs.\n\\end{document}\n");
    write(dir / "maketex.toml",
        "[content-filters]\n"
        "aux = '^\\\\gdef\\s*\\\\runs'\n");

    auto r = m_cli->run({ "--max-passes", "3" }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.out << r.err;
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));
}

TEST_F(CliTest, jobnameConfigAndFlagNameThePublishedPdf)
{
    auto dir = project("job");
    write(dir / "main.tex", kHello);
    write(dir / "maketex.toml", "jobname = \"paper\"\n");

    auto r1 = m_cli->run({ }, dir);
    EXPECT_EQ(r1.exitCode, 0) << r1.err;
    EXPECT_TRUE(fs::exists(dir / "paper.pdf"));

    auto r2 = m_cli->run({ "--jobname", "draft" }, dir);
    EXPECT_EQ(r2.exitCode, 0) << r2.err;
    EXPECT_TRUE(fs::exists(dir / "draft.pdf"));

    auto cleaned = m_cli->run({ "clean", "--jobname", "draft", "--all" }, dir);
    EXPECT_EQ(cleaned.exitCode, 0) << cleaned.err;
    EXPECT_FALSE(fs::exists(dir / "draft.pdf"));
    EXPECT_TRUE(fs::exists(dir / "paper.pdf"));
}

TEST_F(CliTest, subfilesChapterDoesNotCauseRootAmbiguity)
{
    auto dir = project("subfiles");
    write(dir / "thesis.tex",
        "\\documentclass{article}\n\\begin{document}\nThesis.\n\\end{document}\n");
    write(dir / "chapter1.tex",
        "\\documentclass[thesis.tex]{subfiles}\n\\begin{document}\nChapter.\n\\end{document}\n");

    auto r = m_cli->run({ }, dir);
    EXPECT_EQ(r.exitCode, 0) << r.out << r.err;
    EXPECT_TRUE(fs::exists(dir / "thesis.pdf"));
    EXPECT_FALSE(fs::exists(dir / "chapter1.pdf"));
}

TEST_F(CliTest, nomenclBuiltInToolRunsOnNloFile)
{
    if (!maketex::integration::hasProgram("makeindex"))
        GTEST_SKIP() << "not installed: makeindex";
    auto dir = project("nomencl");
    // Minimal nomencl.ist so the test runs even on BasicTeX without nomencl.sty.
    write(dir / "nomencl.ist",
        "keyword \"\\\\nomenclatureentry\"\n"
        "preamble \"\\\\begin{itemize}\\n\"\n"
        "postamble \"\\n\\\\end{itemize}\\n\"\n");
    write(dir / "main.tex",
        "\\documentclass{article}\n"
        "\\newwrite\\nlofile\n"
        "\\begin{document}\n"
        "\\immediate\\openout\\nlofile=\\jobname.nlo\n"
        "\\immediate\\write\\nlofile{\\string\\nomenclatureentry{a@Alpha}{1}}\n"
        "\\immediate\\closeout\\nlofile\n"
        "\\InputIfFileExists{\\jobname.nls}{}{pending}\n"
        "\\end{document}\n");

    auto cold = m_cli->run({ }, dir);
    EXPECT_EQ(cold.exitCode, 0) << cold.out << cold.err;
    EXPECT_TRUE(fs::exists(dir / ".maketex" / "main.nls"));
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));

    auto warm = m_cli->run({ }, dir);
    EXPECT_EQ(warm.exitCode, 0) << warm.out << warm.err;
    EXPECT_TRUE(contains(warm.out, "up to date")) << warm.out;
}

TEST_F(CliTest, preambleCacheReusesFmtPreservesLineNumbersAndInvalidatesOnPreambleEdit)
{
    auto dir = project("fmtcache");
    write(dir / "macros.tex", "\\newcommand{\\greeting}{Hello v1}\n");
    write(dir / "main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{amsmath}\n"
        "\\input{macros}\n"
        "\\begin{document}\n"
        "Line 5: \\greeting.\n"
        "\\end{document}\n");

    auto cold = m_cli->run({ "--preamble-cache" }, dir);
    ASSERT_EQ(cold.exitCode, 0) << cold.out << cold.err;
    const fs::path fmt = dir / ".maketex" / "main-preamble.fmt";
    ASSERT_TRUE(fs::exists(fmt));
    ASSERT_TRUE(fs::exists(dir / "main.pdf"));
    auto fmtMtime = fs::last_write_time(fmt);

    auto warm = m_cli->run({ "--preamble-cache" }, dir);
    EXPECT_EQ(warm.exitCode, 0) << warm.err;
    EXPECT_TRUE(contains(warm.out, "up to date")) << warm.out;

    // Body-only edit must reuse the cached .fmt without re-dumping it.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    write(dir / "main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{amsmath}\n"
        "\\input{macros}\n"
        "\\begin{document}\n"
        "Line 5 edited: \\greeting.\n"
        "\\end{document}\n");
    auto bodyEdit = m_cli->run({ "--preamble-cache" }, dir);
    EXPECT_EQ(bodyEdit.exitCode, 0) << bodyEdit.out << bodyEdit.err;
    EXPECT_EQ(fs::last_write_time(fmt), fmtMtime) << ".fmt was unnecessarily re-dumped on body edit";

    // Errors in the body must still point at the exact line in main.tex.
    write(dir / "main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{amsmath}\n"
        "\\input{macros}\n"
        "\\begin{document}\n"
        "Line 5 ok.\n"
        "Line 6: \\nosuchmacro\n"
        "\\end{document}\n");
    auto bad = m_cli->run({ "--preamble-cache" }, dir);
    EXPECT_EQ(bad.exitCode, 1);
    EXPECT_TRUE(contains(bad.err, "main.tex:6")) << bad.err;

    // Editing an \input'd preamble file invalidates and re-dumps the .fmt,
    // even when main.tex itself is untouched since the last successful build.
    write(dir / "main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{amsmath}\n"
        "\\input{macros}\n"
        "\\begin{document}\n"
        "Line 5: \\greeting.\n"
        "\\end{document}\n");
    ASSERT_EQ(m_cli->run({ "--preamble-cache" }, dir).exitCode, 0);
    fmtMtime = fs::last_write_time(fmt);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    write(dir / "macros.tex", "\\newcommand{\\greeting}{Hello v2}\n");
    auto preambleEdit = m_cli->run({ "--preamble-cache" }, dir);
    EXPECT_EQ(preambleEdit.exitCode, 0) << preambleEdit.out << preambleEdit.err;
    EXPECT_FALSE(contains(preambleEdit.out, "up to date")) << preambleEdit.out;
    EXPECT_NE(fs::last_write_time(fmt), fmtMtime) << ".fmt was not invalidated on macros.tex edit";

    // Clean sweeps the .fmt cache artifacts too.
    auto cleaned = m_cli->run({ "clean" }, dir);
    EXPECT_EQ(cleaned.exitCode, 0) << cleaned.err;
    EXPECT_FALSE(fs::exists(fmt));
}

TEST_F(CliTest, preambleCacheWorksWithMultiPassDraftAndSpacesInPath)
{
    // Combines Stage B cold -draftmode, Stage D .fmt caching, and paths with spaces.
    auto dir = project("My Cached Paper");
    write(dir / "main.tex",
        "\\documentclass{article}\n"
        "\\usepackage{amsmath}\n"
        "\\begin{document}\n"
        "\\tableofcontents\n"
        "\\section{Intro}\\label{sec:i}\n"
        "See Section~\\ref{sec:i}.\n"
        "\\end{document}\n");

    auto cold = m_cli->run({ "--preamble-cache" }, dir);
    ASSERT_EQ(cold.exitCode, 0) << cold.out << cold.err;
    EXPECT_TRUE(contains(cold.out, "done in 2 passes")) << cold.out;
    EXPECT_TRUE(fs::exists(dir / ".maketex" / "main-preamble.fmt"));
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));

    auto warm = m_cli->run({ "--preamble-cache" }, dir);
    EXPECT_EQ(warm.exitCode, 0) << warm.out << warm.err;
    EXPECT_TRUE(contains(warm.out, "up to date")) << warm.out;
}

TEST_F(CliTest, preambleCacheFallsBackGracefullyOnUnsupportedPreambleOrEngine)
{
    // A preamble containing \begin{filecontents*} cannot be safely skipped;
    // maketex must fall back to a normal build without creating a .fmt.
    auto dir = project("fmtfallback");
    write(dir / "main.tex",
        "\\begin{filecontents*}{note.txt}\n"
        "From filecontents.\n"
        "\\end{filecontents*}\n"
        "\\documentclass{article}\n"
        "\\begin{document}\n"
        "Hello.\n"
        "\\end{document}\n");

    auto res = m_cli->run({ "--preamble-cache" }, dir);
    ASSERT_EQ(res.exitCode, 0) << res.out << res.err;
    EXPECT_TRUE(fs::exists(dir / "main.pdf"));
    EXPECT_TRUE(fs::exists(dir / ".maketex" / "note.txt"));
    EXPECT_FALSE(fs::exists(dir / ".maketex" / "main-preamble.fmt"));

    // Non-pdflatex engines ignore --preamble-cache gracefully.
    if (maketex::integration::hasProgram("xelatex")) {
        auto xeDir = project("fmtxe");
        write(xeDir / "main.tex", kHello);
        auto xe = m_cli->run({ "--engine", "xelatex", "--preamble-cache" }, xeDir);
        ASSERT_EQ(xe.exitCode, 0) << xe.out << xe.err;
        EXPECT_TRUE(fs::exists(xeDir / "main.pdf"));
        EXPECT_FALSE(fs::exists(xeDir / ".maketex" / "main-preamble.fmt"));
    }
}
