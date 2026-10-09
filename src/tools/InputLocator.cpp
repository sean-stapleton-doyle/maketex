#include "maketex/tools/InputLocator.h"

#include "maketex/Process.h"

#include <cstdlib>
#include <system_error>

namespace maketex {

namespace fs = std::filesystem;

std::vector<std::pair<std::string, std::string>> toolSearchEnv(const fs::path& texDir)
{
    std::vector<std::pair<std::string, std::string>> env;
    for (const char* var : { "BIBINPUTS", "BSTINPUTS", "TEXINPUTS", "INDEXSTYLE" }) {
        std::string value = texDir.string() + ":";
        if (const char* existing = std::getenv(var); existing && *existing)
            value += existing;
        env.emplace_back(var, std::move(value));
    }
    return env;
}

std::optional<fs::path> locateToolInput(std::string_view name,
    std::string_view extension,
    const EngineInvocation& inv,
    std::optional<std::string_view> kpsewhichFormat)
{
    fs::path file { name };
    if (file.empty())
        return std::nullopt;
    if (!file.has_extension())
        file += extension;

    std::error_code ec;
    if (file.is_absolute())
        return fs::is_regular_file(file, ec) ? std::optional { file } : std::nullopt;
    for (const auto& dir : { inv.texFile.parent_path(), inv.outputDir }) {
        auto candidate = dir / file;
        if (fs::is_regular_file(candidate, ec))
            return candidate;
    }
    if (!kpsewhichFormat)
        return std::nullopt;

    try {
        std::vector<std::string> args {
            "kpsewhich",
            "-format=" + std::string(*kpsewhichFormat),
            file.string(),
        };
        auto env = toolSearchEnv(inv.texFile.parent_path());
        auto result = Process(args, inv.outputDir, env).wait();
        auto found = result.stdoutOutput;
        while (!found.empty() && (found.back() == '\n' || found.back() == '\r'))
            found.pop_back();
        if (result.exitCode != 0 || found.empty())
            return std::nullopt;
        fs::path resolved { found };
        // kpsewhich answers relative to its working directory (outputDir)
        // for files found via a relative search path entry.
        return resolved.is_absolute() ? resolved : inv.outputDir / resolved;
    } catch (const std::system_error&) {
        return std::nullopt; // kpsewhich not installed
    }
}

} // namespace maketex
