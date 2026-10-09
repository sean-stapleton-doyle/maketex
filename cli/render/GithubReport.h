#pragma once

#include "maketex/BuildDriver.h"

#include <iosfwd>

namespace maketex::cli {

// Emits one GitHub Actions workflow command per diagnostic
// (::error file=…,line=…,title=LaTeX::message) followed by a one-line plain
// summary. File paths are emitted relative to the current working directory
// when possible — GitHub resolves annotation paths against the workspace
// root, which is the CWD in a checkout step.
void printGithubReport(const DriverResult& result, std::ostream& out);

} // namespace maketex::cli
