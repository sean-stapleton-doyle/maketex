#pragma once

#include "maketex/RunResult.h"
#include "maketex/StateDatabase.h"
#include "maketex/graph/FileNode.h"

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace maketex::driver {

// Content digests keyed by weakly-canonical path string.
using Digests = std::map<std::string, Hash>;

// Files the engine writes for the user, for maketex, or for other tools, never
// content a following pass depends on: excluded from convergence snapshots
// (hashing a large PDF every pass would be pure cost). The .run.xml is logreq's
// request list for external build tools; biblatex flips its "active" flags from
// pass to pass.
bool isFinalProduct(const std::filesystem::path& p);

// Formats a human-readable failure message for a failed/abnormal subprocess run:
// "biber is not installed (not found on PATH)", "latex exited with code 1", ...
std::string formatRunFailure(std::string_view what,
    std::string_view program,
    int exitCode,
    bool notFound,
    bool timedOut,
    bool cancelled);

template <typename Inv>
std::string failureMessage(std::string_view what, const RunResult<Inv>& r)
{
    return formatRunFailure(what, r.program, r.exitCode, r.notFound, r.timedOut, r.cancelled);
}

// The first engine read-back file (role Generated) that existed before the pass
// and was changed or removed by it. Write-only outputs (.idx, .bcf) are the
// tools' business, and files created for the first time are caught by the log.
std::optional<std::filesystem::path> firstChangedReadBack(
    std::span<const FileNode* const> files,
    const Digests& before,
    const Digests& after);

// Signals collected after one engine pass + tool wave.
struct PassSignals {
    int pass = 0; // 0-based
    bool logAskedRerun = false;
    std::optional<std::filesystem::path> readBackChanged;
    std::optional<std::string> appearedInput;
    std::optional<std::filesystem::path> editedDuringPass;
    std::vector<std::string> toolChanges;
    bool draftFirstPass = false;
    bool xdvSplit = false;
};

// Ordered human-readable reasons why another engine pass is required. Empty
// means the build has converged.
std::vector<std::string> rerunReasons(const PassSignals& s);

// Joins reasons with "; " for BuildEvent::detail.
std::string joinRerunReasons(std::span<const std::string> reasons);

// Formats "pass N: reason1; reason2" for DriverResult::rerunReasons.
std::string formatRerunReason(int nextPass, std::span<const std::string> reasons);

} // namespace maketex::driver
