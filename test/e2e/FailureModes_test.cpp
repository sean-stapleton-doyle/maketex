// Ways a build fails that only show up end to end.

#include "E2eSupport.h"

#include "maketex/Process.h"

#include <chrono>
#include <csignal>
#include <thread>

using namespace maketex::e2e;
namespace fs = std::filesystem;

namespace {

class FailureModesTest : public E2eTest { };

} // namespace

TEST_F(FailureModesTest, documentThatNeverConvergesHitsThePassLimit)
{
    // Every pass writes a different .aux (a counter of runs), so the build
    // can never settle.
    auto dir = project("restless");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\makeatletter\n\\providecommand\\runs{0}\n"
        "\\AtEndDocument{\\immediate\\write\\@auxout{\\gdef\\string\\runs{\\the\\numexpr\\runs+1\\relax}}}\n"
        "\\makeatother\n\\begin{document}\nRun \\runs.\n\\end{document}\n");
    auto r = m_cli->run({ "--max-passes", "3" }, dir);
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_TRUE(contains(r.err, "did not converge after 3 passes")) << r.err;

    // And it is never reported up to date afterwards.
    EXPECT_FALSE(contains(m_cli->run({ "--max-passes", "3" }, dir).out, "up to date"));
}

TEST_F(FailureModesTest, missingFigureIsReportedAtItsLine)
{
    auto dir = project("nofig");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\usepackage{graphicx}\n\\begin{document}\n"
        "Text.\n\\includegraphics{missing-figure}\n\\end{document}\n");
    auto r = m_cli->run({ }, dir);
    EXPECT_EQ(r.exitCode, 1);
    EXPECT_TRUE(contains(r.err, "main.tex:5")) << r.err;
    EXPECT_TRUE(contains(r.err, "missing-figure")) << r.err;
    EXPECT_FALSE(contains(r.err, "hint: install")) << "a user's file is not a package: " << r.err;
}

TEST_F(FailureModesTest, stoppedBuildExitsWithTheSignalAndLeavesNoEngine)
{
    auto dir = project("stopped");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\def\\x{\\x}\\x\n\\end{document}\n");
    std::vector<std::string> argv { CliRunner::binary().string() };
    auto env = m_cli->environment();
    maketex::Process build(argv, dir, env);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!fs::exists(dir / ".maketex" / "main.log") && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    build.kill(); // SIGTERM to maketex alone
    auto result = build.wait();

    EXPECT_EQ(result.exitCode, 128 + SIGTERM);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto left = maketex::Process(std::vector<std::string> { "pgrep", "-f", dir.filename().string() }).wait();
    EXPECT_NE(left.exitCode, 0) << "engine still running: " << left.stdoutOutput;
}
