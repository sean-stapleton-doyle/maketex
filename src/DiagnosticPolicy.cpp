#include "maketex/DiagnosticPolicy.h"

#include <algorithm>
#include <format>

namespace maketex {

std::optional<std::string> applyDiagnosticPolicy(
    std::vector<Diagnostic>& diagnostics,
    const DiagnosticPolicy& policy)
{
    if (!policy.ignore.empty()) {
        std::erase_if(diagnostics, [&](const Diagnostic& d) {
            return d.severity != Diagnostic::Severity::Error
                && !d.category.empty()
                && std::ranges::contains(policy.ignore, d.category);
        });
    }

    int promoted = 0;
    std::string firstCategory;
    for (auto& d : diagnostics) {
        if (d.severity != Diagnostic::Severity::Warning)
            continue;
        bool fail = policy.warningsAsErrors
            || (!d.category.empty() && std::ranges::contains(policy.failOn, d.category));
        if (fail) {
            d.severity = Diagnostic::Severity::Error;
            if (firstCategory.empty())
                firstCategory = d.category;
            ++promoted;
        }
    }
    if (promoted == 0)
        return std::nullopt;
    if (policy.warningsAsErrors)
        return std::format("{} warning{} treated as error{}",
            promoted, promoted == 1 ? "" : "s", promoted == 1 ? "" : "s");
    return std::format("failed on {} warning{}", firstCategory, promoted == 1 ? "" : "s");
}

} // namespace maketex
