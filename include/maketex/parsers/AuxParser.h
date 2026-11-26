#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace maketex {

// A glossary registered in the .aux via \@newglossary{name}{log}{out}{in}.
struct GlossarySpec {
    std::string triggerExt; // e.g. "glo", "acn", "syg" (written by LaTeX)
    std::string outputExt; // e.g. "gls", "acr", "syi" (written by makeglossaries)
};

// The bibliography- and glossary-relevant content of a .aux file and every
// child .aux it pulls in via \@input (one per \include'd chapter), in document
// order.
struct AuxRecord {
    // Bare database names from "\bibdata{}" lines — usually no .bib
    // extension. The caller locates the actual files.
    std::vector<std::string> bibDataFiles;
    // Style names from "\bibstyle{}" lines.
    std::vector<std::string> bibStyles;
    // Every \citation, \bibdata, and \bibstyle line, newline-joined. This is
    // exactly what bibtex reads from the .aux, so its digest changes only
    // when bibtex's output could — unlike the whole file, which LaTeX
    // rewrites with page numbers and labels on every pass.
    std::string bibtexSignature;
    // Glossaries declared via \@newglossary (including custom types like
    // \newglossary[slg]{symbols}{syi}{syg}{List of Symbols}).
    std::vector<GlossarySpec> glossaries;
    // bib2gls resources from \glsxtr@resource{options}{basename}:
    // .bib files named by src=..., expected output basenames (<base>.glstex),
    // and the newline-joined \glsxtr@resource / \glsxtr@record signature.
    std::vector<std::string> glsBibFiles;
    std::vector<std::string> glsOutputs;
    std::string bib2glsSignature;
    // The .aux tree holds something no rerun check covers: anything beyond
    // \relax, labels and table-of-contents entries (LaTeX warns when those
    // change), bibliography records (the tools key on them), and the
    // bookkeeping lines of the kernel, hyperref, and babel. Packages that
    // store their own data there (counters, totals) don't warn, so a pass
    // that writes such content must be followed by one that reads it.
    bool hasUncheckedContent = false;
};

class AuxParser {
public:
    // Parses path and, recursively, the child .aux files it \@input's
    // (resolved relative to path's directory). A missing child is skipped:
    // it appears once the engine has processed that \include.
    std::expected<AuxRecord, std::error_code> parse(
        const std::filesystem::path& path) const;

private:
    static std::string_view extractBraces(std::string_view line, std::string_view prefix);

    // Templated on the callback (cf. detail::forEachLine) so the call site is
    // monomorphised — avoids a per-call std::function indirection.
    template <typename Fn>
    static void splitComma(std::string_view s, Fn&& fn);
};

template <typename Fn>
void AuxParser::splitComma(std::string_view s, Fn&& fn)
{
    size_t start = 0;
    while (true) {
        auto end = s.find(',', start);
        if (end == std::string_view::npos) {
            std::forward<Fn>(fn)(s.substr(start));
            break;
        }
        std::forward<Fn>(fn)(s.substr(start, end - start));
        start = end + 1;
    }
}

} // namespace maketex
