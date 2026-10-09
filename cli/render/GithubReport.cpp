#include "render/GithubReport.h"

#include <filesystem>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    // Workflow-command escaping rules: data escapes %, \r, \n; property
    // values additionally escape , and : (they delimit the property list).
    std::string escapeData(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
            case '%':
                out += "%25";
                break;
            case '\r':
                out += "%0D";
                break;
            case '\n':
                out += "%0A";
                break;
            default:
                out += c;
            }
        }
        return out;
    }

    std::string escapeProperty(std::string_view s)
    {
        std::string out;
        for (char c : escapeData(s)) {
            switch (c) {
            case ',':
                out += "%2C";
                break;
            case ':':
                out += "%3A";
                break;
            default:
                out += c;
            }
        }
        return out;
    }

    const char* commandName(Diagnostic::Severity severity)
    {
        switch (severity) {
        case Diagnostic::Severity::Error:
            return "error";
        case Diagnostic::Severity::Warning:
            return "warning";
        case Diagnostic::Severity::Info:
            return "notice";
        }
        return "error";
    }

    // GitHub resolves annotation paths against the workspace root, which is
    // the process CWD under actions/checkout. Prefer the resolved absolute
    // path re-rooted there; fall back to the diagnostic's display path.
    std::string annotationPath(const Diagnostic& d)
    {
        if (!d.sourcePath.empty()) {
            std::error_code ec;
            auto rel = d.sourcePath.lexically_relative(
                fs::weakly_canonical(fs::current_path(), ec));
            if (!rel.empty() && *rel.begin() != "..")
                return rel.generic_string();
        }
        return d.file;
    }

} // namespace

void printGithubReport(const DriverResult& result, std::ostream& out)
{
    for (const auto& d : result.diagnostics) {
        out << "::" << commandName(d.severity);
        auto path = annotationPath(d);
        char sep = ' ';
        if (!path.empty()) {
            out << sep << "file=" << escapeProperty(path);
            sep = ',';
        }
        if (d.line > 0) {
            out << sep << "line=" << d.line;
            sep = ',';
        }
        out << sep << "title=LaTeX";
        auto message = d.hint.empty() ? d.message : d.message + "\nhint: " + d.hint;
        out << "::" << escapeData(message) << "\n";
    }

    std::string passes = std::to_string(result.passes) + " pass"
        + (result.passes == 1 ? "" : "es");
    if (result.upToDate)
        out << "maketex: up to date\n";
    else if (result.success)
        out << "maketex: done in " << passes << "\n";
    else
        out << "maketex: failed after " << passes << ": " << result.lastError
            << "\n";
}

} // namespace maketex::cli
