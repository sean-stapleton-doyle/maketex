#include "maketex/DiagnosticFormatter.h"

#include <ostream>

namespace maketex::diagnostics {

void format(const std::vector<Diagnostic>& diagnostics, std::ostream& out,
    Verbosity verbosity)
{
    for (const auto& d : diagnostics) {
        switch (d.severity) {
        case Diagnostic::Severity::Error:
            out << "error";
            break;
        case Diagnostic::Severity::Warning:
            out << "warning";
            break;
        case Diagnostic::Severity::Info:
            out << "note";
            break;
        }

        if (!d.file.empty()) {
            out << ": " << d.file;
            if (d.line >= 0)
                out << ":" << d.line;
        }

        out << ": " << d.message << "\n";
        if (!d.hint.empty())
            out << "  hint: " << d.hint << "\n";

        if (verbosity == Verbosity::Verbose && !d.rawContext.empty())
            out << d.rawContext << "\n";
    }
}

} // namespace maketex::diagnostics
