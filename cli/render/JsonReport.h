#pragma once

#include "maketex/Build.h"

#include <iosfwd>

namespace maketex::cli {

// Serialises a finished build to a single JSON document on out, terminated
// by a newline. The schema is versioned via "formatVersion"; consumers
// should tolerate added fields.
void printJsonReport(const DriverResult& result, const BuildOptions& options,
    std::ostream& out);

} // namespace maketex::cli
