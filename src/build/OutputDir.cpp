#include "maketex/build/OutputDir.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::build {

bool shouldMirrorForFile(const fs::path& p)
{
    static constexpr std::array<std::string_view, 11> kExts = {
        ".tex", ".eps", ".ps", ".svg", ".pdf", ".png", ".jpg", ".jpeg",
        ".tikz", ".pgf", ".bib"
    };
    auto ext = p.extension().string();
    return std::ranges::find(kExts, ext) != kExts.end();
}

bool shouldSkipMirrorDir(std::string_view dirName)
{
    return dirName.starts_with('.') || dirName == "node_modules" || dirName == "__pycache__";
}

void mirrorSourceDirectories(const fs::path& texDir, const fs::path& outDir)
{
    std::error_code ec;
    if (fs::equivalent(texDir, outDir, ec))
        return;
    auto outCanonical = fs::weakly_canonical(outDir, ec);
    // Bounded: running in a huge directory (a home dir) must stay cheap.
    constexpr int kMaxEntries = 20000;
    int seen = 0;
    fs::recursive_directory_iterator it(texDir,
        fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (++seen > kMaxEntries)
            break;
        const auto& entry = *it;
        auto name = entry.path().filename().string();
        if (entry.is_directory(ec)) {
            // Hidden dirs (.git, .maketex), the output dir, and
            // dependency trees never hold document inputs.
            if (shouldSkipMirrorDir(name)
                || fs::weakly_canonical(entry.path(), ec) == outCanonical)
                it.disable_recursion_pending();
            continue;
        }
        if (!shouldMirrorForFile(entry.path()))
            continue;
        auto rel = fs::relative(entry.path().parent_path(), texDir, ec);
        if (!ec && !rel.empty() && rel != ".")
            fs::create_directories(outDir / rel, ec);
    }
}

void ignoreInVersionControl(const fs::path& outDir)
{
    auto gitignore = outDir / ".gitignore";
    std::error_code ec;
    if (!fs::exists(gitignore, ec))
        std::ofstream(gitignore) << "# Created by maketex: build outputs and state.\n*\n";
}

} // namespace maketex::build
