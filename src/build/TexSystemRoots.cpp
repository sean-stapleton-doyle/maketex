#include "maketex/build/TexSystemRoots.h"

#include "maketex/Build.h"

#include <unistd.h>

#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::build {

namespace {

    // The distribution and its generated caches. TEXMFHOME is left out
    // on purpose: it holds the user's own packages, whose edits must
    // trigger rebuilds. TEXMFLOCAL sits outside TEXMFROOT on a full
    // MacTeX/TeX Live install, and Debian splits out TEXMFMAIN.
    constexpr std::array<std::string_view, 8> kTexRootVars = {
        "TEXMFROOT",
        "TEXMFDIST",
        "TEXMFMAIN",
        "TEXMFLOCAL",
        "TEXMFSYSVAR",
        "TEXMFSYSCONFIG",
        "TEXMFVAR",
        "TEXMFCONFIG",
    };

} // namespace

std::optional<fs::path> findOnPath(std::string_view program, const EnvLookup& env)
{
    const EnvLookup lookup = env ? env : processEnv();
    auto pathEnv = lookup("PATH");
    if (!pathEnv)
        return std::nullopt;
    std::string_view rest(*pathEnv);
    std::error_code ec;
    while (!rest.empty()) {
        auto colon = rest.find(':');
        auto dir = rest.substr(0, colon);
        if (!dir.empty()) {
            fs::path candidate = fs::path(dir) / program;
            if (fs::is_regular_file(candidate, ec) && ::access(candidate.c_str(), X_OK) == 0)
                return candidate;
        }
        if (colon == std::string_view::npos)
            break;
        rest.remove_prefix(colon + 1);
    }
    return std::nullopt;
}

fs::path defaultCacheDir(const EnvLookup& env)
{
    const EnvLookup lookup = env ? env : processEnv();
    if (auto xdg = lookup("XDG_CACHE_HOME"); xdg && !xdg->empty())
        return fs::path(*xdg) / "maketex";
    if (auto home = lookup("HOME"); home && !home->empty())
        return fs::path(*home) / ".cache" / "maketex";
    return { };
}

std::string fileStampToken(const fs::path& path)
{
    std::error_code ec;
    auto mtime = fs::last_write_time(path, ec);
    if (ec)
        return { };
    auto size = fs::file_size(path, ec);
    if (ec)
        return { };
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        mtime.time_since_epoch())
                  .count();
    return std::format("{}:{}", size, ns);
}

std::string rootsCacheKey(const fs::path& kpsePath, std::string_view stamp,
    const EnvLookup& env)
{
    const EnvLookup lookup = env ? env : processEnv();
    std::string cacheKey = "v1|" + kpsePath.string() + "|" + std::string(stamp);
    for (auto var : kTexRootVars) {
        cacheKey += '|';
        if (auto v = lookup(var))
            cacheKey += *v;
    }
    if (auto cnf = lookup("TEXMFCNF")) {
        cacheKey += '|';
        cacheKey += *cnf;
    }
    return cacheKey;
}

std::vector<fs::path> parseKpsewhichRoots(std::string_view stdoutOutput)
{
    std::vector<fs::path> found;
    std::string_view rest = stdoutOutput;
    while (!rest.empty()) {
        auto bar = rest.find('|');
        auto value = rest.substr(0, bar);
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
            value.remove_suffix(1);
        if (value.starts_with("!!"))
            value.remove_prefix(2);
        // Unset variables expand to nothing or stay "$VAR"; path
        // lists ({a,b}, a:b) are not single roots.
        if (!value.empty() && !value.starts_with('$')
            && value.find_first_of("{}:;") == std::string_view::npos)
            found.emplace_back(value);
        if (bar == std::string_view::npos)
            break;
        rest.remove_prefix(bar + 1);
    }
    return found;
}

std::optional<std::vector<fs::path>> readRootsCache(
    const fs::path& cacheFile, std::string_view expectedKey)
{
    if (cacheFile.empty())
        return std::nullopt;
    std::ifstream in(cacheFile);
    std::string header;
    if (!std::getline(in, header) || header != expectedKey)
        return std::nullopt;
    std::vector<fs::path> cached;
    std::string line;
    while (std::getline(in, line))
        if (!line.empty())
            cached.emplace_back(std::move(line));
    return cached;
}

void writeRootsCache(const fs::path& cacheFile, std::string_view key,
    const std::vector<fs::path>& roots)
{
    if (cacheFile.empty())
        return;
    std::error_code ec;
    fs::create_directories(cacheFile.parent_path(), ec);
    fs::path tmp = cacheFile;
    tmp += ".tmp-" + std::to_string(::getpid());
    {
        std::ofstream out(tmp);
        out << key << '\n';
        for (const auto& p : roots)
            out << p.string() << '\n';
    }
    fs::rename(tmp, cacheFile, ec);
    if (ec)
        fs::remove(tmp, ec);
}

std::vector<fs::path> resolveTexSystemRoots(
    const ProcessRunner& runner, const EnvLookup& env)
{
    const EnvLookup lookup = env ? env : processEnv();
    auto kpse = findOnPath("kpsewhich", lookup);
    if (!kpse)
        return { };

    std::string cacheKey = rootsCacheKey(*kpse, fileStampToken(*kpse), lookup);
    const fs::path dir = defaultCacheDir(lookup);
    const fs::path cacheFile = dir.empty() ? fs::path { } : dir / "tex_roots.cache";
    if (auto cached = readRootsCache(cacheFile, cacheKey))
        return *cached;

    std::string query = "-expand-var=";
    for (auto var : kTexRootVars) {
        query += '$';
        query += var;
        query += '|';
    }

    std::vector<fs::path> found;
    try {
        ProcessLaunchSpec spec {
            .argv = { kpse->string(), query },
        };
        auto result = runner ? runner(spec) : runSystemProcess(spec);
        if (result.exitCode != 0)
            return found;
        found = parseKpsewhichRoots(result.stdoutOutput);
    } catch (const std::system_error&) {
        // kpsewhich not installed or not executable: nothing to ignore.
        return found;
    }

    writeRootsCache(cacheFile, cacheKey, found);
    return found;
}

} // namespace maketex::build

namespace maketex {

std::vector<fs::path> texSystemRoots()
{
    // Constant for the life of the process, and cached on disk across CLI
    // invocations so `✓ up to date` runs don't pay ~25 ms to spawn kpsewhich.
    static const std::vector<fs::path> roots = build::resolveTexSystemRoots();
    return roots;
}

} // namespace maketex
