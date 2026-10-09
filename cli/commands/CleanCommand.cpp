#include "commands/CleanCommand.h"

#include "CleanPlan.h"

#include <filesystem>
#include <iostream>
#include <set>

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    // Merges b into a, keeping each plan's sort order and dropping
    // duplicates (the legacy pass can name the same products).
    void mergePlans(CleanPlan& a, const CleanPlan& b)
    {
        std::set<fs::path> seen(a.remove.begin(), a.remove.end());
        for (const auto& p : b.remove)
            if (seen.insert(p).second)
                a.remove.push_back(p);
        a.skippedOutside.insert(a.skippedOutside.end(),
            b.skippedOutside.begin(), b.skippedOutside.end());
    }

} // namespace

int runCleanCommand(const EffectiveOptions& options)
{
    const BuildOptions& build = options.build;
    const fs::path outputDir = build.resolvedOutputDir();
    const fs::path texDir = build.texFile.parent_path();
    const std::string jobName = build.texFile.stem().string();

    // In default-output mode the PDF/SyncTeX copies published beside the
    // tex file are this build's products too, and a pre-.maketex state
    // database may still sit next to the sources.
    std::vector<fs::path> published;
    if (build.usesDefaultOutputDir()) {
        published = {
            texDir / (jobName + ".pdf"),
            texDir / (jobName + ".synctex.gz"),
            texDir / ".maketex.db",
        };
    }

    auto plan = computeCleanPlan(scanBuildFiles(build), build.texFile,
        outputDir, jobName, options.extras.cleanAll,
        options.extras.cleanExtraExtensions, published);

    // Builds made before the hidden default directory existed left their
    // aux files next to the sources. When the default location has no build
    // record but the tex directory does, sweep the legacy layout as well.
    if (build.usesDefaultOutputDir()
        && !fs::exists(outputDir / (jobName + ".fls"))
        && fs::exists(texDir / (jobName + ".fls"))) {
        BuildOptions legacy = build;
        legacy.outputDir = texDir;
        mergePlans(plan,
            computeCleanPlan(scanBuildFiles(legacy), build.texFile, texDir,
                jobName, options.extras.cleanAll,
                options.extras.cleanExtraExtensions));
    }

    for (const auto& p : plan.skippedOutside)
        std::cerr << "maketex: skipping " << p.string()
                  << " (outside the output directory)\n";

    if (plan.remove.empty()) {
        std::cout << "maketex: nothing to clean\n";
        return 0;
    }

    if (options.extras.dryRun) {
        for (const auto& p : plan.remove)
            std::cout << "would remove " << p.string() << "\n";
        std::cout << "maketex: " << plan.remove.size() << " file"
                  << (plan.remove.size() == 1 ? "" : "s")
                  << " (dry run, nothing deleted)\n";
        return 0;
    }

    int failures = 0;
    std::uintmax_t removed = 0;
    for (const auto& p : plan.remove) {
        std::error_code ec;
        // remove_all: the pythontex output directory is a candidate too.
        auto count = fs::remove_all(p, ec);
        if (ec) {
            std::cerr << "maketex: cannot remove " << p.string() << ": "
                      << ec.message() << "\n";
            ++failures;
        } else {
            removed += count;
            std::cout << "removed " << p.string() << "\n";
        }
    }

    // The hidden default directory is ours; drop it once nothing is left
    // inside (only --all empties it — otherwise the PDF/db remain).
    if (build.usesDefaultOutputDir()) {
        std::error_code ec;
        if (fs::is_directory(outputDir, ec) && fs::is_empty(outputDir, ec))
            fs::remove(outputDir, ec);
    }

    std::cout << "maketex: removed " << removed << " file"
              << (removed == 1 ? "" : "s") << "\n";
    return failures == 0 ? 0 : 1;
}

} // namespace maketex::cli
