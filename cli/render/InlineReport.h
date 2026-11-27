#pragma once

#include "maketex/Build.h"

#include <ftxui/dom/elements.hpp>

#include <iosfwd>
#include <string>

namespace maketex::cli {

// One diagnostic as a styled row (severity badge, file:line, message, and
// the raw log context when verbose). Shared by the inline report and the
// watch TUI's diagnostics pane.
ftxui::Element diagnosticRow(const Diagnostic& diagnostic, bool verbose);

// "lualatex (loads fontspec)" — the engine and, when it was detected, why.
std::string engineLabel(EngineKind engine, const std::string& reason);

// The complete one-shot build report: header, diagnostics, status footer.
ftxui::Element buildReport(const BuildResult& result,
    const BuildOptions& options, bool verbose);

// Renders doc at fitted size and writes it to `out` (or stdout).
void printInline(const ftxui::Element& doc, std::ostream& out);
void printInline(const ftxui::Element& doc);

// Renders a finished build in human format — styled FTXUI inline output when
// `styled` is true, plain diagnostics + summary otherwise. Shared by
// BuildCommand and WatchSession plain mode.
void printHumanReport(const BuildResult& result, const BuildOptions& options,
    bool verbose, bool styled, std::ostream& out, std::ostream& err);

// Pure styling predicate and its process-environment wrapper.
bool useStyledOutput(bool plainFlag, bool isTty, bool noColor);
bool useStyledOutput(bool plainFlag);

} // namespace maketex::cli
