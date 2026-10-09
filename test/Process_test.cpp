#include "maketex/Process.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using maketex::Process;

TEST(Process, exitZeroOnSuccess)
{
    Process p({ std::vector<std::string> { "/usr/bin/true" } });
    auto r = p.wait();
    EXPECT_EQ(r.exitCode, 0);
}

TEST(Process, exitOneOnFailure)
{
    Process p({ std::vector<std::string> { "/usr/bin/false" } });
    auto r = p.wait();
    EXPECT_EQ(r.exitCode, 1);
}

TEST(Process, capturesStdout)
{
    Process p({ std::vector<std::string> { "/bin/echo", "hello world" } });
    auto r = p.wait();
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.stdoutOutput, "hello world\n");
    EXPECT_TRUE(r.stderrOutput.empty());
}

TEST(Process, capturesStderr)
{
    Process p({ std::vector<std::string> { "/bin/sh", "-c", "echo err >&2" } });
    auto r = p.wait();
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_TRUE(r.stdoutOutput.empty());
    EXPECT_EQ(r.stderrOutput, "err\n");
}

TEST(Process, capturesBothStreams)
{
    Process p({ std::vector<std::string> { "/bin/sh", "-c", "echo out; echo err >&2" } });
    auto r = p.wait();
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.stdoutOutput, "out\n");
    EXPECT_EQ(r.stderrOutput, "err\n");
}

TEST(Process, killTerminatesProcess)
{
    Process p({ std::vector<std::string> { "/bin/sleep", "100" } });
    p.kill();
    auto r = p.wait();
    // Killed by signal — not a normal exit.
    EXPECT_EQ(r.exitCode, -1);
}

TEST(Process, destructorDoesNotHangOnRunningProcess)
{
    // If the destructor fails to reap, the test runner will hang — which is
    // itself a failure signal. No explicit assertion needed beyond completion.
    Process p({ std::vector<std::string> { "/bin/sleep", "100" } });
    // p goes out of scope here; destructor must send SIGTERM and waitpid.
}

TEST(Process, throwsOnBadExecutable)
{
    EXPECT_THROW(
        Process(std::vector<std::string> { "/nonexistent/binary" }),
        std::system_error);
}

TEST(Process, largeOutput)
{
    // dd produces exactly 1 MiB of zero bytes on stdout.
    Process p({ std::vector<std::string> { "/bin/dd", "if=/dev/zero", "bs=1024", "count=1024" } });
    auto r = p.wait();
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.stdoutOutput.size(), 1024UL * 1024UL);
}

TEST(Process, extraEnvAppendsNewVariable)
{
    std::vector<std::pair<std::string, std::string>> env {
        { "MAKETEX_TEST_VAR", "hello" },
    };
    Process p(std::vector<std::string> { "/bin/sh", "-c", "echo $MAKETEX_TEST_VAR" },
        { }, env);
    auto r = p.wait();
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.stdoutOutput, "hello\n");
}

TEST(Process, extraEnvOverridesInheritedVariable)
{
    // HOME is always present in the inherited environment; the override must
    // replace it, not duplicate it.
    std::vector<std::pair<std::string, std::string>> env {
        { "HOME", "/maketex-test-home" },
    };
    Process p(std::vector<std::string> { "/bin/sh", "-c", "echo $HOME" }, { }, env);
    auto r = p.wait();
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_EQ(r.stdoutOutput, "/maketex-test-home\n");
}

TEST(Process, emptyExtraEnvInheritsEnvironment)
{
    Process p(std::vector<std::string> { "/bin/sh", "-c", "echo $HOME" });
    auto r = p.wait();
    EXPECT_EQ(r.exitCode, 0);
    EXPECT_NE(r.stdoutOutput, "\n");
}
