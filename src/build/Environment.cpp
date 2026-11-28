#include "maketex/build/Environment.h"

#include <algorithm>
#include <cstdlib>
#include <ranges>

namespace fs = std::filesystem;

namespace maketex::build {

EnvLookup processEnv()
{
    return [](std::string_view name) -> std::optional<std::string> {
        std::string key(name);
        if (const char* value = std::getenv(key.c_str()))
            return std::string(value);
        return std::nullopt;
    };
}

std::vector<std::pair<std::string, std::string>> searchPathEnv(
    const BuildOptions& options, const EnvLookup& env)
{
    const EnvLookup lookup = env ? env : processEnv();
    std::vector<std::pair<std::string, std::string>> result = options.extraEnv;
    auto addSearchPath = [&](std::string_view var, const std::vector<std::string>& dirs) {
        if (dirs.empty())
            return;
        std::string value;
        for (const auto& d : dirs) {
            value += d;
            value += ':';
        }
        auto existingIt = std::ranges::find(result, var,
            &std::pair<std::string, std::string>::first);
        if (existingIt != result.end()) {
            value += existingIt->second;
            existingIt->second = std::move(value);
            return;
        }
        if (auto inherited = lookup(var); inherited && !inherited->empty())
            value += *inherited;
        result.emplace_back(std::string(var), std::move(value));
    };
    addSearchPath("TEXINPUTS", options.searchPaths.texInputs);
    addSearchPath("BIBINPUTS", options.searchPaths.bibInputs);
    addSearchPath("BSTINPUTS", options.searchPaths.bstInputs);
    addSearchPath("INDEXSTYLE", options.searchPaths.indexStyle);
    return result;
}

std::string prependTexFormats(const fs::path& outDir, const EnvLookup& env)
{
    const EnvLookup lookup = env ? env : processEnv();
    std::string texFormats = outDir.string() + ":";
    if (auto inherited = lookup("TEXFORMATS"); inherited && !inherited->empty())
        texFormats += *inherited;
    return texFormats;
}

} // namespace maketex::build
