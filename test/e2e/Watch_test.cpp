// `maketex watch --no-tui` as a long-running process: start it, change the
// sources, and see it rebuild.

#include "E2eSupport.h"

#include "maketex/Process.h"

#include <chrono>
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
