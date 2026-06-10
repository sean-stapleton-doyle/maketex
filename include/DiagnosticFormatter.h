#pragma once

#include "Diagnostic.h"

#include <iosfwd>
#include <vector>

namespace maketex::diagnostics {

// Formats a vector of Diagnostics for terminal output.
// At normal verbosity: severity, file:line, and message only.
// With verbose=true: also prints rawContext for each diagnostic.
void format(const std::vector<Diagnostic>& diagnostics, std::ostream& out, bool verbose = false);

} // namespace maketex::diagnostics
