#pragma once

#include <functional>
#include <string>

namespace maketex {

// Progress milestone fired during a BuildDriver run so a UI can show live
// status. Delivered synchronously on the build thread — observers shared
// with another thread must do their own marshaling.
struct BuildEvent {
    enum class Kind {
        PassStarted, // an engine pass is about to run; pass is 1-based
        PassFinished, // the engine pass exited; success reflects its exit code
        ToolStarted, // an auxiliary tool is about to run; toolName is set
        ToolFinished, // the tool exited; toolName and success are set
        SkippedUpToDate, // incremental check passed — the build is a no-op
        RerunScheduled, // another pass follows; pass is its number, detail why
    };

    Kind kind = Kind::PassStarted;
    int pass = 0; // engine pass number; 0 for tool events
    std::string toolName;
    bool success = true;
    std::string detail; // RerunScheduled: the reasons, "; "-separated
};

using BuildObserver = std::function<void(const BuildEvent&)>;

} // namespace maketex
