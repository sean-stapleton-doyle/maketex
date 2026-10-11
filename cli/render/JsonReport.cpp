#include "render/JsonReport.h"

#include "render/InlineReport.h"

#include <nlohmann/json.hpp>

#include <algorithm>

#include <ostream>

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    const char* severityName(Diagnostic::Severity severity)
    {
        switch (severity) {
        case Diagnostic::Severity::Error:
            return "error";
        case Diagnostic::Severity::Warning:
            return "warning";
        case Diagnostic::Severity::Info:
            return "info";
        }
        return "error";
    }

} // namespace

void printJsonReport(const BuildResult& result, const BuildOptions& options,
    std::ostream& out)
{
    nlohmann::json doc;
    doc["formatVersion"] = 1;
    doc["success"] = result.success;
    doc["upToDate"] = result.upToDate;
    doc["passes"] = result.passes;
    doc["rerunReasons"] = result.rerunReasons;
    doc["error"] = result.success ? nlohmann::json(nullptr)
                                  : nlohmann::json(result.lastError);
    doc["document"] = options.texFile.string();
    doc["engine"] = engineKindName(result.engine);
    doc["engineReason"] = result.engineReason.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(result.engineReason);

    // Only a PDF that actually reached its user-facing location: a failed
    // publish (read-only directory) reports null rather than a stale path.
    auto pdf = std::ranges::find_if(result.published,
        [](const fs::path& p) { return p.extension() == ".pdf"; });
    doc["pdf"] = pdf != result.published.end() ? nlohmann::json(pdf->string())
                                               : nlohmann::json(nullptr);

    auto& diagnostics = doc["diagnostics"] = nlohmann::json::array();
    for (const auto& d : result.diagnostics) {
        nlohmann::json entry;
        entry["severity"] = severityName(d.severity);
        entry["file"] = d.file.empty() ? nlohmann::json(nullptr)
                                       : nlohmann::json(d.file);
        entry["path"] = d.sourcePath.empty()
            ? nlohmann::json(nullptr)
            : nlohmann::json(d.sourcePath.string());
        entry["line"] = d.line > 0 ? nlohmann::json(d.line)
                                   : nlohmann::json(nullptr);
        entry["message"] = d.message;
        entry["category"] = d.category;
        entry["context"] = d.rawContext;
        entry["hint"] = d.hint.empty() ? nlohmann::json(nullptr) : nlohmann::json(d.hint);
        diagnostics.push_back(std::move(entry));
    }

    out << doc.dump(2) << "\n";
}

} // namespace maketex::cli
