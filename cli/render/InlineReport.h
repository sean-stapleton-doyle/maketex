#pragma once

#include "maketex/Build.h"

#include <ftxui/dom/elements.hpp>

namespace maketex::cli {

// One diagnostic as a styled row (severity badge, file:line, message, and
// the raw log context when verbose). Shared by the inline report and the
// watch TUI's diagnostics pane.
ftxui::Element diagnosticRow(const Diagnostic& diagnostic, bool verbose);

// The complete one-shot build report: header, diagnostics, status footer.
ftxui::Element buildReport(const DriverResult& result,
    const BuildOptions& options, bool verbose);

// Renders doc at fitted size and writes it to stdout.
void printInline(const ftxui::Element& doc);

// True when stdout is a terminal and neither --plain nor NO_COLOR forbid
// styling.
bool useStyledOutput(bool plainFlag);

// "pdflatex", "xelatex", or "lualatex" — for display.
std::string_view engineKindName(EngineKind kind);

} // namespace maketex::cli
