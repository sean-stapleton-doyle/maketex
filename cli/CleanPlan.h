#pragma once

#include "maketex/Build.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace maketex::cli {

struct CleanPlan {
    // Existing files (and the pythontex directory) to delete, sorted.
    std::vector<std::filesystem::path> remove;
    // Recorded engine outputs that escaped outputDir — reported, never deleted.
    std::vector<std::filesystem::path> skippedOutside;
};

// Decides what `maketex clean` removes. Candidates are the engine outputs
// recorded in the last build's .fls (when available) plus the static
// jobName.<ext> list — mandatory, because tool outputs (.bbl, .ind, ...) are
// only ever *read* by the engine and never appear as .fls OUTPUT records.
//
// Without `all` the PDF, SyncTeX data, and the state database survive
// (latexmk -c); with `all` they go too (latexmk -C). texFile itself is never
// removed, and nothing outside outputDir is touched — except the
// publishedArtifacts the caller names (the PDF/SyncTeX copies the default
// output mode publishes beside the tex file, plus the legacy state
// database): protected without `all`, removed with it.
CleanPlan computeCleanPlan(const std::optional<BuildFileSets>& fileSets,
    const std::filesystem::path& texFile,
    const std::filesystem::path& outputDir,
    const std::string& jobName,
    bool all,
    const std::vector<std::string>& extraExtensions,
    const std::vector<std::filesystem::path>& publishedArtifacts = { },
    bool userDirectory = false);

// Merges `extra` into `target`, preserving order and dropping duplicate
// removal paths (e.g. when sweeping a legacy pre-.maketex layout).
void mergePlans(CleanPlan& target, const CleanPlan& extra);

// When `dbPath` is scheduled for removal but other jobs still have state in
// it, removes `dbPath` from `plan.remove` and returns true (the caller should
// forget `jobName` in place instead of deleting the database).
bool preserveSharedDatabase(CleanPlan& plan,
    const std::filesystem::path& dbPath,
    const std::string& jobName,
    const std::vector<std::string>& recordedJobs);

} // namespace maketex::cli
