#include "FixtureHarness.h"

#include "../test_utils.h"
#include "maketex/Process.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace maketex::integration {

namespace fs = std::filesystem;

namespace {

    // Fixture sources; everything else in testdata/ is a leftover build
    // product from manual runs and must not leak into a test's copy.
    constexpr std::array kSourceExtensions = {
        std::string_view { ".tex" },
        std::string_view { ".bib" },
        std::string_view { ".ist" },
        std::string_view { ".eps" },
        std::string_view { ".png" },
        std::string_view { ".jpg" },
    };

    bool isSource(const fs::path& p)
    {
        auto ext = p.extension().string();
        for (auto source : kSourceExtensions)
            if (ext == source)
                return true;
        return false;
    }

    std::string readFile(const fs::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // "failures/err-x" → "failures_err-x": one path component.
    std::string sanitized(std::string name)
    {
        std::ranges::replace(name, '/', '_');
        return name;
    }

} // namespace

bool hasProgram(const std::string& program)
{
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv)
        return false;
    std::string_view rest(pathEnv);
    while (!rest.empty()) {
        auto colon = rest.find(':');
        auto dir = rest.substr(0, colon);
        std::error_code ec;
        auto candidate = fs::path(dir) / program;
        if (!dir.empty() && fs::is_regular_file(candidate, ec))
            return true;
        if (colon == std::string_view::npos)
            break;
        rest.remove_prefix(colon + 1);
    }
    return false;
}

bool hasTexFile(const std::string& file)
{
    try {
        auto result = Process(std::vector<std::string> { "kpsewhich", file }).wait();
        return result.exitCode == 0 && !result.stdoutOutput.empty();
    } catch (const std::system_error&) {
        return false;
    }
}

FixtureCopy::FixtureCopy(const std::string& name)
    : m_dir(test_utils::uniqueTempDir("maketex_it_" + sanitized(name)))
{
    fs::path source = fs::path(MAKETEX_TESTDATA_DIR) / name;
    if (!fs::is_directory(source))
        throw std::runtime_error("no such fixture: " + source.string());
    fs::remove_all(m_dir);
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (!entry.is_regular_file() || !isSource(entry.path()))
            continue;
        auto dest = m_dir / fs::relative(entry.path(), source);
        fs::create_directories(dest.parent_path());
        fs::copy_file(entry.path(), dest);
    }
    // Canonical so paths reported by the build compare equal to ours even
    // where the temp dir is behind a symlink (macOS /var → /private/var).
    m_dir = fs::canonical(m_dir);
}

FixtureCopy::~FixtureCopy()
{
    std::error_code ec;
    fs::remove_all(m_dir, ec);
}

BuildOptions FixtureCopy::options(const std::string& texFile) const
{
    BuildOptions options;
    options.texFile = m_dir / texFile;
    return options;
}

BuildRun FixtureCopy::build(BuildOptions options) const
{
    BuildRun run;
    auto userObserver = std::move(options.observer);
    options.observer = [&run, &userObserver](const BuildEvent& event) {
        if (event.kind == BuildEvent::Kind::ToolStarted)
            run.toolsRun.push_back(event.toolName);
        if (event.kind == BuildEvent::Kind::SkippedUpToDate)
            run.upToDate = true;
        if (userObserver)
            userObserver(event);
    };
    run.result = runBuild(options);
    return run;
}

void FixtureCopy::append(const std::string& relative, const std::string& text) const
{
    std::ofstream(path(relative), std::ios::app | std::ios::binary) << text;
}

void FixtureCopy::replace(const std::string& relative, const std::string& from,
    const std::string& to) const
{
    auto content = readFile(path(relative));
    auto pos = content.find(from);
    if (pos == std::string::npos)
        throw std::runtime_error("replace: '" + from + "' not in " + relative);
    content.replace(pos, from.size(), to);
    write(relative, content);
}

void FixtureCopy::write(const std::string& relative, const std::string& content) const
{
    auto p = path(relative);
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << content;
}

} // namespace maketex::integration
