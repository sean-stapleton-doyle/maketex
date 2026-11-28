#pragma once

#include <string>
#include <string_view>

namespace maketex::diagnostics {

std::string categorizeError(std::string_view msg);
std::string categorizeWarning(std::string_view msg);

// Strips engine boilerplate from an error message: the "LaTeX Error: "
// prefix and a trailing period.
std::string cleanErrorMessage(std::string message);

// Extracts the line number from a "... on input line N." suffix, or -1.
int extractInputLine(std::string_view msg);

// Strips an " on input line N" suffix or a bare trailing period.
void stripInputLineSuffix(std::string& msg);

} // namespace maketex::diagnostics
