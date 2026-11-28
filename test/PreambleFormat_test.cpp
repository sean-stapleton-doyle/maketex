#include "maketex/build/PreambleFormat.h"

#include "maketex/Blake3Hasher.h"
#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using maketex::Blake3Hasher;
using maketex::BuildOptions;
using maketex::EngineArguments;
using maketex::Hash;
using maketex::ProcessLaunchSpec;
using maketex::ProcessResult;
using maketex::ProcessRunner;
using maketex::StateDatabase;
using maketex::build::extractDumpablePreamble;
using maketex::build::extractDumpablePreambleText;
using maketex::build::formatDumpArgs;
using maketex::build::hexDigest;
using maketex::build::preambleCacheMaterial;
using maketex::build::preparePreambleFormat;

namespace {

class PreambleFormatTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_preamblefmt_test"; }
};

std::string slurp(const fs::path& p)
{
    std::ostringstream s;
    s << std::ifstream(p).rdbuf();
    return s.str();
}

} // namespace

TEST(PreambleFormatPureTest, extractDumpablePreambleTextEdgeCases)
{
    auto basic = extractDumpablePreambleText(
        "\\documentclass{article}\n"
        "% \\begin{commented}\n"
        "\\usepackage{amsmath}\n"
        "\\begin{document}\n"
        "Hello\n"
        "\\end{document}\n");
    ASSERT_TRUE(basic.has_value());
    EXPECT_EQ(*basic,
        "\\documentclass{article}\n"
        "% \\begin{commented}\n"
        "\\usepackage{amsmath}\n");

    // Prefix on the same line as \begin{document} is included.
    auto sameLine = extractDumpablePreambleText("\\documentclass{article}\\begin{document}Hi");
    ASSERT_TRUE(sameLine.has_value());
    EXPECT_EQ(*sameLine, "\\documentclass{article}");

    // Unsuitable cases return nullopt.
    EXPECT_FALSE(extractDumpablePreambleText("\\documentclass{article}\nNo body\n").has_value());
    EXPECT_FALSE(extractDumpablePreambleText("\\begin{document}\nEmpty preamble\n").has_value());
    EXPECT_FALSE(extractDumpablePreambleText(
        "\\documentclass{article}\n"
        "\\begin{filecontents}{a.bib}\n"
        "\\end{filecontents}\n"
        "\\begin{document}\n")
            .has_value());
    EXPECT_FALSE(extractDumpablePreambleText(
        "\\documentclass{article}\n"
        "\\usepackage{minted}\n"
        "\\begin{document}\n")
            .has_value());
}

TEST(PreambleFormatPureTest, hexDigestEncodesLowercaseHex)
{
    Hash h { };
    h[0] = 0x0a;
    h[1] = 0xf0;
    h[31] = 0x5c;
    auto hex = hexDigest(h);
    ASSERT_EQ(hex.size(), 64u);
    EXPECT_EQ(hex.substr(0, 4), "0af0");
    EXPECT_EQ(hex.substr(62, 2), "5c");
}

TEST(PreambleFormatPureTest, cacheMaterialReflectsEveryInput)
{
    const std::vector<std::string> opts { "-file-line-error" };
    const std::vector<std::pair<std::string, std::string>> env { { "TEXINPUTS", "styles:" } };
    const std::vector<std::pair<std::string, std::string>> inputs { { "/project/a.sty", "deadbeef" } };

    auto base = preambleCacheMaterial("pdflatex", false, opts, env, "\\documentclass{article}", inputs);
    EXPECT_NE(base, preambleCacheMaterial("other", false, opts, env, "\\documentclass{article}", inputs));
    EXPECT_NE(base, preambleCacheMaterial("pdflatex", true, opts, env, "\\documentclass{article}", inputs));
    EXPECT_NE(base, preambleCacheMaterial("pdflatex", false, { }, env, "\\documentclass{article}", inputs));
    EXPECT_NE(base, preambleCacheMaterial("pdflatex", false, opts, { }, "\\documentclass{article}", inputs));
    EXPECT_NE(base, preambleCacheMaterial("pdflatex", false, opts, env, "\\documentclass{book}", inputs));
    EXPECT_NE(base, preambleCacheMaterial("pdflatex", false, opts, env, "\\documentclass{article}", { }));
}

TEST(PreambleFormatPureTest, formatDumpArgsBuildsIniCommandLine)
{
    auto args = formatDumpArgs(
        "", "/project/.maketex", "main-preamble", false, "/project/.maketex/.main-preamble.tex");
    EXPECT_EQ(args,
        (std::vector<std::string> {
            "pdflatex",
            "-ini",
            "-recorder",
            "-interaction=nonstopmode",
            "-halt-on-error",
            "-output-directory=/project/.maketex",
            "-jobname=main-preamble",
            "&pdflatex",
            "\\input{\"/project/.maketex/.main-preamble.tex\"}",
        }));

    auto withEscape = formatDumpArgs(
        "/opt/pdflatex", "/project/out", "doc-preamble", true, "/project/out/.doc-preamble.tex");
    EXPECT_EQ(withEscape.front(), "/opt/pdflatex");
    EXPECT_NE(std::ranges::find(withEscape, "-shell-escape"), withEscape.end());
}

TEST_F(PreambleFormatTest, preparePreambleFormatCachesAndInvalidatesViaFakeRunner)
{
    EXPECT_FALSE(extractDumpablePreamble(m_dir / "missing.tex").has_value());

    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    fs::path texFile = writeFile("main.tex",
        "\\documentclass{article}\n\\input{defs.tex}\n\\begin{document}Hi\\end{document}\n");
    fs::path defsFile = writeFile("defs.tex", "\\newcommand{\\foo}{1}\n");

    BuildOptions options { .texFile = texFile, .preambleCache = true };
    EngineArguments engineArgs;
    StateDatabase stateDb(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;
    const std::vector<fs::path> noSystemRoots;

    int runnerCalls = 0;
    ProcessRunner fakeIni = [&](const ProcessLaunchSpec& spec) {
        ++runnerCalls;
        EXPECT_EQ(spec.workingDir, m_dir);
        fs::path fmtPath = outDir / "main-preamble.fmt";
        fs::path flsPath = outDir / "main-preamble.fls";
        fs::path preambleTex = outDir / ".main-preamble.tex";
        EXPECT_NE(slurp(preambleTex).find("\\dump"), std::string::npos);
        std::ofstream(fmtPath) << "fake-fmt";
        std::ofstream(flsPath)
            << "PWD " << m_dir.string() << "\n"
            << "INPUT " << preambleTex.string() << "\n"
            << "INPUT " << defsFile.string() << "\n";
        return ProcessResult { .exitCode = 0 };
    };

    // Cold build invokes -ini and caches key.
    EXPECT_EQ(preparePreambleFormat(options, engineArgs, outDir, "main", { },
                  stateDb, hasher, fakeIni, std::span<const fs::path>(noSystemRoots)),
        "main-preamble");
    EXPECT_EQ(runnerCalls, 1);

    // Warm hit reuses format without spawning -ini.
    EXPECT_EQ(preparePreambleFormat(options, engineArgs, outDir, "main", { },
                  stateDb, hasher, fakeIni, std::span<const fs::path>(noSystemRoots)),
        "main-preamble");
    EXPECT_EQ(runnerCalls, 1);

    // Editing a preamble dependency invalidates cache and re-dumps.
    writeFile("defs.tex", "\\newcommand{\\foo}{2}\n");
    EXPECT_EQ(preparePreambleFormat(options, engineArgs, outDir, "main", { },
                  stateDb, hasher, fakeIni, std::span<const fs::path>(noSystemRoots)),
        "main-preamble");
    EXPECT_EQ(runnerCalls, 2);

    // Deleting a preamble dependency records MISSING and invalidates cache.
    fs::remove(defsFile);
    EXPECT_EQ(preparePreambleFormat(options, engineArgs, outDir, "main", { },
                  stateDb, hasher, fakeIni, std::span<const fs::path>(noSystemRoots)),
        "main-preamble");
    EXPECT_EQ(runnerCalls, 3);
}

TEST_F(PreambleFormatTest, preparePreambleFormatFallsBackOnFailures)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    StateDatabase stateDb(maketex::test_utils::makeMemoryDb());
    Blake3Hasher hasher;
    const std::vector<fs::path> noSystemRoots;

    // Undumpable preamble never calls runner.
    fs::path mintedTex = writeFile("minted.tex",
        "\\documentclass{article}\n\\usepackage{minted}\n\\begin{document}Hi\\end{document}\n");
    int calls = 0;
    ProcessRunner counting = [&](const ProcessLaunchSpec&) {
        ++calls;
        return ProcessResult { .exitCode = 0 };
    };
    EXPECT_TRUE(preparePreambleFormat(BuildOptions { .texFile = mintedTex }, { },
        outDir, "minted", { }, stateDb, hasher, counting,
        std::span<const fs::path>(noSystemRoots))
            .empty());
    EXPECT_EQ(calls, 0);

    // Non-zero exit removes stale .fmt and returns empty.
    fs::path texFile = writeFile("fail.tex",
        "\\documentclass{article}\n\\begin{document}Hi\\end{document}\n");
    std::ofstream(outDir / "fail-preamble.fmt") << "stale";
    ProcessRunner nonZero = [](const ProcessLaunchSpec&) {
        return ProcessResult { .exitCode = 1 };
    };
    EXPECT_TRUE(preparePreambleFormat(BuildOptions { .texFile = texFile }, { },
        outDir, "fail", { }, stateDb, hasher, nonZero,
        std::span<const fs::path>(noSystemRoots))
            .empty());
    EXPECT_FALSE(fs::exists(outDir / "fail-preamble.fmt"));

    // Exit 0 without producing .fmt returns empty.
    EXPECT_TRUE(preparePreambleFormat(BuildOptions { .texFile = texFile }, { },
        outDir, "fail", { }, stateDb, hasher, counting,
        std::span<const fs::path>(noSystemRoots))
            .empty());

    // System error returns empty cleanly.
    ProcessRunner throwing = [](const ProcessLaunchSpec&) -> ProcessResult {
        throw std::system_error(ENOENT, std::generic_category(), "missing pdflatex");
    };
    EXPECT_TRUE(preparePreambleFormat(BuildOptions { .texFile = texFile }, { },
        outDir, "fail", { }, stateDb, hasher, throwing,
        std::span<const fs::path>(noSystemRoots))
            .empty());
}
