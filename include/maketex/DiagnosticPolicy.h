#pragma once

#include "maketex/Diagnostic.h"

#include <optional>
#include <string>
#include <vector>

namespace maketex {

// Controls how warnings affect the build outcome and which non-error
// diagnostic categories are muted.
struct DiagnosticPolicy {
    bool warningsAsErrors = false;
    // Warning categories that fail the build even when warningsAsErrors is off
    // (e.g. "undefined-citation", "undefined-reference").
    std::vector<std::string> failOn;
    // Non-error categories to suppress (e.g. "overfull-hbox", "underfull-hbox").
    std::vector<std::string> ignore;
};

// Filters ignored non-error diagnostics and promotes warnings to errors
// according to policy. Returns an error summary when the policy fails the
// build, or nullopt when the build still passes.
std::optional<std::string> applyDiagnosticPolicy(
    std::vector<Diagnostic>& diagnostics,
    const DiagnosticPolicy& policy);

} // namespace maketex
