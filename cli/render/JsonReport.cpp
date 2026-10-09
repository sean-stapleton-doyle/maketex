#include "render/JsonReport.h"

#include "render/InlineReport.h"

#include <nlohmann/json.hpp>

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

void printJsonReport(const DriverResult& result, const BuildOptions& options,
    std::ostream& out)
{
    nlohmann::json doc;
    doc["formatVersion"] = 1;
    doc["success"] = result.success;
    doc["upToDate"] = result.success && result.passes == 0;
    doc["passes"] = result.passes;
    doc["error"] = result.success ? nlohmann::json(nullptr)
                                  : nlohmann::json(result.lastError);
    doc["document"] = options.texFile.string();
    doc["engine"] = engineKindName(options.engine);

    // Default mode publishes the PDF beside the tex file; an explicit
    // output dir keeps it inside.
    fs::path pdfDir = options.usesDefaultOutputDir()
        ? options.texFile.parent_path()
        : options.outputDir;
    fs::path pdf = pdfDir / (options.texFile.stem().string() + ".pdf");
    doc["pdf"] = result.success ? nlohmann::json(pdf.string())
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
        diagnostics.push_back(std::move(entry));
    }

    out << doc.dump(2) << "\n";
}

} // namespace maketex::cli
