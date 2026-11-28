#pragma once

#include "maketex/Build.h"
#include "maketex/build/Environment.h"

#include <span>
#include <string>
#include <string_view>

namespace maketex::build {

// A readable summary of everything that changes what the engine and tools
// produce. A stored database whose fingerprint differs was built under
// different semantics, so its hashes must not drive incremental decisions.
// Deliberately not hashed — debuggability beats brevity.
std::string buildFingerprint(const BuildOptions& options,
    std::string_view engineName,
    std::span<const std::string_view> toolNames,
    const EnvLookup& env = { });

} // namespace maketex::build
