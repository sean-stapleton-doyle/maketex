#include "render/InlineReport.h"

#include "PathUtil.h"

#include "render/SourceSnippet.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <unistd.h>

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

using namespace ftxui;

namespace maketex::cli {

bool useStyledOutput(bool plainFlag)
{
    return !plainFlag && isatty(STDOUT_FILENO) == 1
        && std::getenv("NO_COLOR") == nullptr;
}

namespace {

    Element severityBadge(Diagnostic::Severity severity)
    {
        switch (severity) {
        case Diagnostic::Severity::Error:
            return text(" error ") | bgcolor(Color::Red) | color(Color::White)
                | bold;
        case Diagnostic::Severity::Warning:
            return text(" warn ") | bgcolor(Color::Yellow) | color(Color::Black);
        case Diagnostic::Severity::Info:
            return text(" info ") | bgcolor(Color::Blue) | color(Color::White);
        }
        return text(" ? ");
    }

} // namespace

Element diagnosticRow(const Diagnostic& diagnostic, bool verbose)
{
    Elements parts { severityBadge(diagnostic.severity), text(" ") };

    if (!diagnostic.file.empty()) {
        std::string location = diagnostic.file;
        if (diagnostic.line > 0)
            location += ":" + std::to_string(diagnostic.line);
        parts.push_back(text(location) | dim);
        parts.push_back(text("  "));
    }
    parts.push_back(text(diagnostic.message));

    Element row = hbox(std::move(parts));

    // Compiler-style source context: quote the offending line when the
    // diagnostic resolved to a real file and line.
    Element snippetRow;
    if (auto snippet = readSourceLine(diagnostic.sourcePath, diagnostic.line))
        snippetRow = hbox(
            text(std::format("  {:>4} │ ", diagnostic.line)) | dim,
            text(std::move(*snippet)));

    Elements lines { row };
    if (snippetRow)
        lines.push_back(std::move(snippetRow));
    if (!diagnostic.hint.empty())
        lines.push_back(hbox(text("  hint: ") | color(Color::Cyan),
            paragraph(diagnostic.hint)));
    if (!verbose || diagnostic.rawContext.empty())
        return lines.size() == 1 ? row : vbox(std::move(lines));

    std::string_view context = diagnostic.rawContext;
    while (!context.empty()) {
        auto newline = context.find('\n');
        auto line = context.substr(0, newline);
        lines.push_back(hbox(text("    │ ") | dim,
            text(std::string(line)) | dim));
        context = newline == std::string_view::npos
            ? std::string_view { }
            : context.substr(newline + 1);
    }
    return vbox(std::move(lines));
}

std::string engineLabel(EngineKind engine, const std::string& reason)
{
    std::string label(engineKindName(engine));
    if (!reason.empty())
        label += " (" + reason + ")";
    return label;
}

Element buildReport(const BuildResult& result, const BuildOptions& options,
    bool verbose)
{
    Elements rows;

    rows.push_back(hbox(text("maketex ") | bold,
        text(options.texFile.filename().string()),
        text("  ·  ") | dim,
        text(engineLabel(result.engine, result.engineReason)) | dim));

    for (const auto& diagnostic : result.diagnostics)
        rows.push_back(diagnosticRow(diagnostic, verbose));
    if (verbose)
        for (const auto& reason : result.rerunReasons)
            rows.push_back(text("  " + reason) | dim);

    std::string passes = passCount(result.passes);
    if (result.upToDate) {
        rows.push_back(hbox(text("✓ ") | color(Color::Green) | bold,
            text("up to date") | color(Color::Green)));
    } else if (result.success && result.passes == 0) {
        rows.push_back(hbox(text("✓ ") | color(Color::Green) | bold,
            text("built") | color(Color::Green),
            text("  ·  tools only, no engine pass needed") | dim));
    } else if (result.success) {
        rows.push_back(hbox(text("✓ ") | color(Color::Green) | bold,
            text("built") | color(Color::Green),
            text("  ·  " + passes) | dim));
    } else {
        rows.push_back(hbox(text("✗ ") | color(Color::Red) | bold,
            text("failed: " + result.lastError) | color(Color::Red),
            text("  ·  " + passes) | dim));
    }

    return vbox(std::move(rows));
}

void printInline(const ftxui::Element& doc)
{
    Element copy = doc;
    auto screen = Screen::Create(Dimension::Fit(copy));
    Render(screen, copy);
    screen.Print();
    std::cout << "\n";
}

} // namespace maketex::cli
