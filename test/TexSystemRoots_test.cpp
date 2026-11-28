#include "maketex/build/TexSystemRoots.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using maketex::ProcessLaunchSpec;
using maketex::ProcessResult;
using maketex::ProcessRunner;
using maketex::build::defaultCacheDir;
using maketex::build::EnvLookup;
using maketex::build::fileStampToken;
using maketex::build::findOnPath;
using maketex::build::parseKpsewhichRoots;
using maketex::build::readRootsCache;
using maketex::build::resolveTexSystemRoots;
using maketex::build::rootsCacheKey;
using maketex::build::writeRootsCache;

namespace {

EnvLookup mapEnv(std::map<std::string, std::string> vars)
{
    return [vars = std::move(vars)](std::string_view key) -> std::optional<std::string> {
        if (auto it = vars.find(std::string(key)); it != vars.end())
            return it->second;
        return std::nullopt;
    };
}

class TexSystemRootsTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_texroots_test"; }

    fs::path makeExecutable(const fs::path& rel) const
    {
        fs::path p = m_dir / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p) << "#!/bin/sh\nexit 0\n";
        fs::permissions(p,
            fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
            fs::perm_options::replace);
        return p;
    }
};

} // namespace

TEST(ParseKpsewhichRootsTest, stripsPrefixesTrimsCrLfAndRejectsInvalidEntries)
{
    const std::string output = "!!/usr/local/texlive/2025|"
                               "/usr/local/texlive/2025/texmf-dist\r\n|"
                               "$TEXMFMAIN|"
                               "|"
                               "{/a,/b}|"
                               "/a:/b|"
                               "/a;/b|"
                               "!!/var/lib/texmf\n";

    auto roots = parseKpsewhichRoots(output);

    EXPECT_EQ(roots,
        (std::vector<fs::path> {
            "/usr/local/texlive/2025",
            "/usr/local/texlive/2025/texmf-dist",
            "/var/lib/texmf",
        }));
}

TEST(ParseKpsewhichRootsTest, handlesSingleEntryWithoutTrailingBar)
{
    auto roots = parseKpsewhichRoots("!!/opt/texlive/texmf-dist\r\n");
    EXPECT_EQ(roots, (std::vector<fs::path> { "/opt/texlive/texmf-dist" }));
    EXPECT_TRUE(parseKpsewhichRoots("").empty());
}

TEST(TexSystemRootsPureTest, defaultCacheDirPrecedence)
{
    EXPECT_EQ(defaultCacheDir(mapEnv({
                  { "XDG_CACHE_HOME", "/project/xdg" },
                  { "HOME", "/project/home" },
              })),
        fs::path("/project/xdg/maketex"));

    EXPECT_EQ(defaultCacheDir(mapEnv({
                  { "XDG_CACHE_HOME", "" },
                  { "HOME", "/project/home" },
              })),
        fs::path("/project/home/.cache/maketex"));

    EXPECT_TRUE(defaultCacheDir(mapEnv({ { "XDG_CACHE_HOME", "" }, { "HOME", "" } })).empty());
    EXPECT_TRUE(defaultCacheDir(mapEnv({ })).empty());
}

TEST(TexSystemRootsPureTest, rootsCacheKeyTracksBinaryStampAndEnv)
{
    const std::string base = rootsCacheKey("/usr/bin/kpsewhich", "100:200", mapEnv({ }));
    EXPECT_NE(base, rootsCacheKey("/opt/bin/kpsewhich", "100:200", mapEnv({ })));
    EXPECT_NE(base, rootsCacheKey("/usr/bin/kpsewhich", "101:200", mapEnv({ })));
    EXPECT_NE(base,
        rootsCacheKey("/usr/bin/kpsewhich", "100:200", mapEnv({ { "TEXMFDIST", "/d" } })));

    // TEXMFCNF unset vs set-to-empty produces distinct keys (matching getenv).
    const std::string emptyCnf = rootsCacheKey(
        "/usr/bin/kpsewhich", "100:200", mapEnv({ { "TEXMFCNF", "" } }));
    EXPECT_NE(base, emptyCnf);
    EXPECT_NE(emptyCnf,
        rootsCacheKey("/usr/bin/kpsewhich", "100:200", mapEnv({ { "TEXMFCNF", "/etc/tex" } })));
}

TEST_F(TexSystemRootsTest, findOnPathSkipsEmptyAndNonExecutableEntries)
{
    fs::path nonExec = m_dir / "bin1" / "tool";
    fs::create_directories(nonExec.parent_path());
    std::ofstream(nonExec) << "plain";
    fs::permissions(nonExec, fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::replace);

    fs::path exec = makeExecutable("bin2/tool");
    const std::string pathVal = ":" + (m_dir / "bin1").string() + "::" + (m_dir / "bin2").string();

    EXPECT_EQ(findOnPath("tool", mapEnv({ { "PATH", pathVal } })), exec);
    EXPECT_FALSE(findOnPath("missing", mapEnv({ { "PATH", pathVal } })).has_value());
    EXPECT_FALSE(findOnPath("tool", mapEnv({ })).has_value());
    EXPECT_FALSE(findOnPath("tool", mapEnv({ { "PATH", "" } })).has_value());
}

TEST_F(TexSystemRootsTest, fileStampTokenAndRootsCacheRoundTrip)
{
    fs::path sample = writeFile("sample.bin", "hello");
    EXPECT_FALSE(fileStampToken(sample).empty());
    EXPECT_TRUE(fileStampToken(m_dir / "nonexistent").empty());

    EXPECT_FALSE(readRootsCache(fs::path { }, "k").has_value());
    writeRootsCache(fs::path { }, "k", { "/a" }); // no-op

    fs::path cacheFile = m_dir / "cache" / "tex_roots.cache";
    const std::vector<fs::path> expected { "/tex/root", "/tex/dist" };
    writeRootsCache(cacheFile, "key-1", expected);

    auto hit = readRootsCache(cacheFile, "key-1");
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, expected);

    EXPECT_FALSE(readRootsCache(cacheFile, "key-2").has_value());

    // Corrupt / empty cache file returns nullopt.
    std::ofstream(cacheFile, std::ios::trunc);
    EXPECT_FALSE(readRootsCache(cacheFile, "key-1").has_value());
}

TEST_F(TexSystemRootsTest, resolveUsesRunnerThenHitsDiskCache)
{
    fs::path kpse = makeExecutable("bin/kpsewhich");
    fs::path cacheRoot = m_dir / "xdg";
    auto env = mapEnv({
        { "PATH", kpse.parent_path().string() },
        { "XDG_CACHE_HOME", cacheRoot.string() },
    });

    int calls = 0;
    ProcessRunner fakeRunner = [&](const ProcessLaunchSpec& spec) {
        ++calls;
        EXPECT_EQ(spec.argv.size(), 2u);
        EXPECT_EQ(spec.argv[0], kpse.string());
        EXPECT_TRUE(spec.argv[1].starts_with("-expand-var=$TEXMFROOT|"));
        return ProcessResult {
            .exitCode = 0,
            .stdoutOutput = "!!/opt/texlive/2025|/opt/texlive/2025/texmf-dist|$TEXMFMAIN|\n",
        };
    };

    auto first = resolveTexSystemRoots(fakeRunner, env);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(first,
        (std::vector<fs::path> { "/opt/texlive/2025", "/opt/texlive/2025/texmf-dist" }));

    auto second = resolveTexSystemRoots(fakeRunner, env);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(second, first);
}

TEST_F(TexSystemRootsTest, resolveHandlesMissingKpsewhichFailureAndThrow)
{
    int calls = 0;
    ProcessRunner countingRunner = [&](const ProcessLaunchSpec&) -> ProcessResult {
        ++calls;
        return { .exitCode = 0, .stdoutOutput = "/tex|" };
    };
    EXPECT_TRUE(resolveTexSystemRoots(countingRunner, mapEnv({ })).empty());
    EXPECT_EQ(calls, 0);

    fs::path kpse = makeExecutable("bin/kpsewhich");
    auto env = mapEnv({
        { "PATH", kpse.parent_path().string() },
        { "XDG_CACHE_HOME", (m_dir / "xdg").string() },
    });

    ProcessRunner failingRunner = [](const ProcessLaunchSpec&) {
        return ProcessResult { .exitCode = 1, .stdoutOutput = "/should/ignore|" };
    };
    EXPECT_TRUE(resolveTexSystemRoots(failingRunner, env).empty());
    EXPECT_FALSE(fs::exists(m_dir / "xdg" / "maketex" / "tex_roots.cache"));

    ProcessRunner throwingRunner = [](const ProcessLaunchSpec&) -> ProcessResult {
        throw std::system_error(ENOENT, std::generic_category(), "spawn failed");
    };
    EXPECT_TRUE(resolveTexSystemRoots(throwingRunner, env).empty());

    // No cache directory configured: still succeeds via runner without writing cache.
    auto noCacheEnv = mapEnv({ { "PATH", kpse.parent_path().string() } });
    auto roots = resolveTexSystemRoots(countingRunner, noCacheEnv);
    EXPECT_EQ(roots, (std::vector<fs::path> { "/tex" }));
}
