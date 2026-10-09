#pragma once

#include "maketex/Diagnostic.h"

#include <iosfwd>
#include <vector>

namespace maketex::diagnostics {

// Output verbosity for format().
enum class Verbosity {
    Normal, // severity, file:line, and message only
    Verbose, // also prints rawContext for each diagnostic
};

// Formats a vector of Diagnostics for terminal output.
void format(const std::vector<Diagnostic>& diagnostics, std::ostream& out,
    Verbosity verbosity = Verbosity::Normal);

} // namespace maketex::diagnostics
