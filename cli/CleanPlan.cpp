#include "CleanPlan.h"

#include "PathUtil.h"

#include <algorithm>
#include <set>
#include <string_view>

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    // Aux files produced by the engine and the standard tool roster.
    constexpr std::string_view kCleanExtensions[] = {
        ".acn", ".acr", ".alg", ".aux", ".bbl", ".bcf", ".blg", ".fls",
        ".glg", ".glo", ".gls", ".glstex", ".idx", ".ilg", ".ind", ".ist",
        ".loa", ".lof", ".log", ".lot", ".nav", ".nlg", ".nlo", ".nls",
        ".out", ".pytxcode", ".run.xml", ".snm", ".toc", ".vrb", ".xdv", ".xdy"
    };

    // Usually generated, but also written by hand (custom index styles) or
    // kept on purpose (a .bbl for arXiv): in a directory holding the user's
    // sources, removed only when the last build recorded writing them.
    constexpr std::string_view kSometimesAuthoredExtensions[] = { ".bbl", ".ist", ".xdy" };

    // Final products, removed only with --all.
    constexpr std::string_view kProductExtensions[] = {
        ".pdf", ".synctex.gz", ".dvi"
    };

} // namespace

CleanPlan computeCleanPlan(const std::optional<BuildFileSets>& fileSets,
    const fs::path& texFile,
    const fs::path& outputDir,
    const std::string& jobName,
    bool all,
    const std::vector<std::string>& extraExtensions,
    const std::vector<fs::path>& publishedArtifacts,
    bool userDirectory)
{
    std::set<fs::path> candidates;

    for (std::string_view ext : kCleanExtensions) {
        if (userDirectory && std::ranges::contains(kSometimesAuthoredExtensions, ext))
            continue;
        candidates.insert(outputDir / (jobName + std::string(ext)));
    }
    for (const auto& ext : extraExtensions)
        candidates.insert(outputDir / (jobName + ext));
    candidates.insert(outputDir / ("pythontex-files-" + jobName));
    // Precompiled preamble format cache artifacts.
    candidates.insert(outputDir / (jobName + "-preamble.fmt"));
    candidates.insert(outputDir / (jobName + "-preamble.fls"));
    candidates.insert(outputDir / (jobName + "-preamble.log"));
    candidates.insert(outputDir / ("." + jobName + "-preamble.tex"));

    // Published artifacts live outside outputDir by design; track their
    // canonical forms so the outside-outputDir safety check can wave
    // exactly these through.
    std::set<fs::path> publishedSet;
    for (const auto& p : publishedArtifacts)
        publishedSet.insert(fs::weakly_canonical(p));

    if (all) {
        for (std::string_view ext : kProductExtensions)
            candidates.insert(outputDir / (jobName + std::string(ext)));
        candidates.insert(outputDir / ".maketex.db");
        candidates.insert(outputDir / ("." + jobName + ".lock"));
        if (!userDirectory)
            candidates.insert(outputDir / ".gitignore"); // written by maketex
        for (const auto& p : publishedArtifacts)
            candidates.insert(p);
    }

    CleanPlan plan;

    if (fileSets) {
        for (const auto& p : fileSets->generated) {
            if (isInside(p, outputDir))
                candidates.insert(p);
            else
                plan.skippedOutside.push_back(p);
        }
    }

    const fs::path canonicalTex = fs::weakly_canonical(texFile);
    std::set<fs::path> protectedPaths;
    if (!all) {
        for (std::string_view ext : kProductExtensions)
            protectedPaths.insert(
                fs::weakly_canonical(outputDir / (jobName + std::string(ext))));
        protectedPaths.insert(fs::weakly_canonical(outputDir / ".maketex.db"));
        for (const auto& p : publishedSet)
            protectedPaths.insert(p);
    }

    for (const auto& candidate : candidates) {
        auto canonical = fs::weakly_canonical(candidate);
        if (canonical == canonicalTex || protectedPaths.contains(canonical))
            continue;
        if (!isInside(candidate, outputDir) && !publishedSet.contains(canonical))
            continue; // jobName.<ext> candidates are always inside; belt and braces
        std::error_code ec;
        if (fs::exists(candidate, ec))
            plan.remove.push_back(candidate);
    }

    std::ranges::sort(plan.remove);
    std::ranges::sort(plan.skippedOutside);
    return plan;
}

void mergePlans(CleanPlan& target, const CleanPlan& extra)
{
    std::set<fs::path> seen(target.remove.begin(), target.remove.end());
    for (const auto& p : extra.remove)
        if (seen.insert(p).second)
            target.remove.push_back(p);
    target.skippedOutside.insert(target.skippedOutside.end(),
        extra.skippedOutside.begin(), extra.skippedOutside.end());
}

bool preserveSharedDatabase(CleanPlan& plan, const fs::path& dbPath,
    const std::string& jobName, const std::vector<std::string>& recordedJobs)
{
    auto it = std::ranges::find(plan.remove, dbPath);
    if (it == plan.remove.end())
        return false;
    bool hasOtherJobs = std::ranges::any_of(recordedJobs,
        [&](const std::string& job) { return job != jobName; });
    if (!hasOtherJobs)
        return false;
    plan.remove.erase(it);
    return true;
}

} // namespace maketex::cli
