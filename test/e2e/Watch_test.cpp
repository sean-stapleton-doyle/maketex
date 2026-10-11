// `maketex watch --no-tui` as a long-running process: start it, change the
// sources, and see it rebuild.

#include "E2eSupport.h"

#include "maketex/Process.h"

#include <chrono>
#include <csignal>
#include <fstream>
#include <optional>
#include <thread>

using namespace maketex::e2e;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

class WatchTest : public E2eTest {
protected:
    // Starts `maketex watch --no-tui` in dir; stopped (and reaped) by the
    // Process destructor.
    std::optional<maketex::Process> startWatch(const fs::path& dir)
    {
        std::vector<std::string> argv { CliRunner::binary().string(), "watch", "--no-tui",
            "--interval-ms", "100" };
        auto env = m_cli->environment();
        return maketex::Process(argv, dir, env);
    }

    // Polls until predicate holds or the deadline passes.
    template <typename Pred>
    static bool eventually(Pred predicate, std::chrono::seconds limit = 60s)
    {
        auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate())
                return true;
            std::this_thread::sleep_for(100ms);
        }
        return false;
    }

    static std::optional<fs::file_time_type> mtime(const fs::path& p)
    {
        std::error_code ec;
        auto t = fs::last_write_time(p, ec);
        return ec ? std::nullopt : std::optional { t };
    }
};

} // namespace

TEST_F(WatchTest, editingTheSourceRebuilds)
{
    auto dir = project("watched");
    write(dir / "main.tex", kHello);
    auto watch = startWatch(dir);

    ASSERT_TRUE(eventually([&] { return fs::exists(dir / "main.pdf"); })) << "first build never published";
    auto firstBuild = mtime(dir / "main.pdf");

    std::this_thread::sleep_for(300ms); // let the watcher settle into polling
    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\nHello, edited.\n\\end{document}\n");

    EXPECT_TRUE(eventually([&] { return mtime(dir / "main.pdf") != firstBuild; }))
        << "no rebuild after editing main.tex";
    watch->kill();
    auto output = watch->wait();
    EXPECT_TRUE(contains(output.stdoutOutput, "changed: main.tex")) << output.stdoutOutput;
}

TEST_F(WatchTest, creatingAMissingInputRebuilds)
{
    auto dir = project("pending");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\input{later}\n\\end{document}\n");
    auto watch = startWatch(dir);

    // The first build fails: later.tex doesn't exist yet.
    std::this_thread::sleep_for(3s);
    ASSERT_FALSE(fs::exists(dir / "main.pdf"));

    write(dir / "later.tex", "Written later.\n");
    EXPECT_TRUE(eventually([&] { return fs::exists(dir / "main.pdf"); }))
        << "creating later.tex did not trigger a rebuild";
}

TEST_F(WatchTest, saveDuringABuildTriggersAnotherBuild)
{
    // A deliberately slow document: the edit lands while pass 1 runs.
    auto doc = [](const std::string& marker) {
        return "\\documentclass{article}\n\\newcount\\n\n"
               "\\loop\\advance\\n by 1 \\ifnum\\n<15000000 \\repeat\n"
               "\\typeout{MARKER-"
            + marker + "}\n\\begin{document}\nSlow.\n\\end{document}\n";
    };
    auto dir = project("slow");
    write(dir / "main.tex", doc("first"));
    auto watch = startWatch(dir);

    // Wait for the engine to start pass 1, then save.
    ASSERT_TRUE(eventually([&] { return fs::exists(dir / ".maketex" / "main.log"); }));
    write(dir / "main.tex", doc("second"));

    auto logHasSecond = [&] {
        std::ifstream log(dir / ".maketex" / "main.log");
        std::string text((std::istreambuf_iterator<char>(log)), { });
        return contains(text, "MARKER-second");
    };
    EXPECT_TRUE(eventually(logHasSecond, 120s)) << "the save made during the build was lost";
}

TEST_F(WatchTest, stoppingWatchStopsTheRunningEngine)
{
    // An engine pass that never ends; SIGTERM to maketex alone (what an
    // editor plugin or process supervisor sends) must not orphan it.
    auto dir = project("forever");
    write(dir / "main.tex",
        "\\documentclass{article}\n\\begin{document}\n\\def\\x{\\x}\\x\n\\end{document}\n");
    auto watch = startWatch(dir);
    ASSERT_TRUE(eventually([&] { return fs::exists(dir / ".maketex" / "main.log"); }));

    watch->kill(); // SIGTERM to maketex only
    watch->wait();

    auto engineAlive = [&] {
        auto r = maketex::Process(std::vector<std::string> { "pgrep", "-f", dir.filename().string() }).wait();
        return r.exitCode == 0; // pgrep found a process mentioning the project
    };
    EXPECT_TRUE(eventually([&] { return !engineAlive(); }, 10s))
        << "pdflatex kept running after maketex was stopped";
}
