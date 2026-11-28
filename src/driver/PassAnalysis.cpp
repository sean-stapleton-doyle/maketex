#include "maketex/driver/PassAnalysis.h"

#include <algorithm>
#include <format>

namespace maketex::driver {

bool isFinalProduct(const std::filesystem::path& p)
{
    auto name = p.filename().string();
    for (std::string_view ext : { ".pdf", ".dvi", ".xdv", ".log", ".fls",
             ".synctex.gz", ".synctex", ".run.xml" })
        if (name.ends_with(ext))
            return true;
    return false;
}

std::string formatRunFailure(std::string_view what,
    std::string_view program,
    int exitCode,
    bool notFound,
    bool timedOut,
    bool cancelled)
{
    if (notFound)
        return std::string(program) + " is not installed (not found on PATH)";
    if (cancelled)
        return "cancelled";
    if (timedOut)
        return std::string(what) + " timed out";
    return std::string(what) + " exited with code " + std::to_string(exitCode);
}

std::optional<std::filesystem::path> firstChangedReadBack(
    std::span<const FileNode* const> files,
    const Digests& before,
    const Digests& after)
{
    auto it = std::ranges::find_if(files, [&](const FileNode* f) {
        if (f->role() != FileRole::Generated)
            return false;
        auto was = before.find(f->path().string());
        auto now = after.find(f->path().string());
        return was != before.end() && (now == after.end() || now->second != was->second);
    });
    if (it == files.end())
        return std::nullopt;
    return (*it)->path();
}

std::vector<std::string> rerunReasons(const PassSignals& s)
{
    std::vector<std::string> reasons;
    if (s.logAskedRerun)
        reasons.emplace_back("LaTeX asked for a rerun");
    if (s.readBackChanged)
        reasons.push_back(s.readBackChanged->filename().string() + " changed");
    if (s.appearedInput)
        reasons.push_back(*s.appearedInput);
    if (s.editedDuringPass)
        reasons.push_back(s.editedDuringPass->filename().string() + " was saved during the pass");
    reasons.insert(reasons.end(), s.toolChanges.begin(), s.toolChanges.end());
    // If pass 1 ran with -draftmode (pdflatex/lualatex) and unexpectedly
    // produced no rerun signals, run one non-draft pass to emit the PDF.
    if (reasons.empty() && s.pass == 0 && s.draftFirstPass && !s.xdvSplit)
        reasons.emplace_back("emitting final PDF after draft pass");
    return reasons;
}

std::string joinRerunReasons(std::span<const std::string> reasons)
{
    std::string why;
    for (const auto& r : reasons)
        why += (why.empty() ? "" : "; ") + r;
    return why;
}

std::string formatRerunReason(int nextPass, std::span<const std::string> reasons)
{
    return std::format("pass {}: {}", nextPass, joinRerunReasons(reasons));
}

} // namespace maketex::driver
