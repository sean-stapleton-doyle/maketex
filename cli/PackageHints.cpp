#include "PackageHints.h"

#include "maketex/Process.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <mutex>
#include <set>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    std::optional<std::string> run(std::vector<std::string> args,
        std::chrono::milliseconds timeout)
    {
        try {
            auto result = Process(args).wait({ .timeout = timeout });
            if (result.exitCode != 0)
                return std::nullopt;
            return std::move(result.stdoutOutput);
        } catch (const std::system_error&) {
            return std::nullopt; // not installed
        }
    }

    std::string trimmed(std::string s)
    {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
            s.pop_back();
        return s;
    }

    // Files and programs common enough to answer without a lookup — also
    // the offline fallback. Programs map to the package that ships them.
    constexpr std::array<std::pair<std::string_view, std::string_view>, 40> kKnown { {
        { "biblatex.sty", "biblatex" },
        { "biber", "biber" },
        { "glossaries.sty", "glossaries" },
        { "makeglossaries", "glossaries" },
        { "makeglossaries-lite", "glossaries" },
        { "glossaries-extra.sty", "glossaries-extra" },
        { "bib2gls", "bib2gls" },
        { "xindy", "xindy" },
        { "minted.sty", "minted" },
        { "pythontex.sty", "pythontex" },
        { "pythontex", "pythontex" },
        { "pgfplots.sty", "pgfplots" },
        { "tikz.sty", "pgf" },
        { "pgf.sty", "pgf" },
        { "luacode.sty", "luacode" },
        { "fontspec.sty", "fontspec" },
        { "unicode-math.sty", "unicode-math" },
        { "polyglossia.sty", "polyglossia" },
        { "siunitx.sty", "siunitx" },
        { "booktabs.sty", "booktabs" },
        { "enumitem.sty", "enumitem" },
        { "cleveref.sty", "cleveref" },
        { "csquotes.sty", "csquotes" },
        { "microtype.sty", "microtype" },
        { "mathtools.sty", "mathtools" },
        { "subcaption.sty", "caption" },
        { "caption.sty", "caption" },
        { "tcolorbox.sty", "tcolorbox" },
        { "todonotes.sty", "todonotes" },
        { "algorithm2e.sty", "algorithm2e" },
        { "imakeidx.sty", "imakeidx" },
        { "nomencl.sty", "nomencl" },
        { "svg.sty", "svg" },
        { "epstopdf.sty", "epstopdf-pkg" },
        { "xcolor.sty", "xcolor" },
        { "geometry.sty", "geometry" },
        { "subfiles.sty", "subfiles" },
        { "makeindex", "makeindex" },
        { "bibtex", "bibtex" },
        { "latexmk", "latexmk" },
    } };

    // Not TeX packages at all.
    std::optional<std::string> nonTexHint(std::string_view missing)
    {
        if (missing == "pygmentize")
            return "minted needs Pygments: pip install pygments (or your system's "
                   "python3-pygments package)";
        if (missing == "inkscape")
            return "the svg package needs Inkscape: install it from https://inkscape.org "
                   "(or brew install --cask inkscape / apt install inkscape)";
        return std::nullopt;
    }

} // namespace

TexDistribution detectDistribution()
{
    using namespace std::chrono_literals;
    if (auto version = run({ "kpsewhich", "--version" }, 5s);
        version && version->contains("MiKTeX"))
        return TexDistribution::MiKTeX;
    auto root = run({ "kpsewhich", "-var-value=TEXMFROOT" }, 5s);
    if (!root)
        return TexDistribution::Unknown;
    // Distribution packages install under /usr/share; upstream TeX Live
    // (and MacTeX/BasicTeX) under /usr/local/texlive or a user prefix.
    if (trimmed(*root).starts_with("/usr/share")) {
        std::error_code ec;
        if (fs::exists("/etc/debian_version", ec))
            return TexDistribution::Debian;
        if (fs::exists("/etc/fedora-release", ec) || fs::exists("/etc/redhat-release", ec))
            return TexDistribution::Fedora;
        return TexDistribution::Unknown;
    }
    return TexDistribution::TexLive;
}

std::optional<std::string> packageProviding(const std::string& file,
    const PackageLookup& lookup)
{
    for (const auto& [name, package] : kKnown)
        if (name == file)
            return std::string(package);
    if (lookup)
        return lookup(file);
    return std::nullopt;
}

std::string installHint(const std::string& missing, TexDistribution distro,
    const PackageLookup& lookup)
{
    if (auto hint = nonTexHint(missing))
        return *hint;
    const bool isFile = fs::path(missing).has_extension();

    switch (distro) {
    case TexDistribution::TexLive: {
        // BasicTeX and MacTeX install under a root-owned prefix.
        auto package = packageProviding(missing, lookup);
        if (package)
            return std::format("install it: sudo tlmgr install {}", *package);
        return std::format("find the package that provides it: "
                           "tlmgr search --global --file /{}",
            missing);
    }
    case TexDistribution::Debian:
        if (!isFile)
            return std::format("install it: sudo apt install {}", missing);
        return std::format("install it: sudo apt install texlive-latex-extra "
                           "(or find the exact package: apt-file search {})",
            missing);
    case TexDistribution::Fedora:
        return isFile ? std::format("install it: sudo dnf install 'tex({})'", missing)
                      : std::format("install it: sudo dnf install texlive-{}",
                            packageProviding(missing, { }).value_or(missing));
    case TexDistribution::MiKTeX:
        return std::format("install it: miktex packages install {}",
            packageProviding(missing, lookup).value_or(fs::path(missing).stem().string()));
    case TexDistribution::Unknown:
        break;
    }
    if (auto package = packageProviding(missing, { }))
        return std::format("install the TeX package `{}`", *package);
    return std::format("{} is not installed", missing);
}

fs::path userCacheDir()
{
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
        return fs::path(xdg) / "maketex";
    if (const char* home = std::getenv("HOME"); home && *home)
        return fs::path(home) / ".cache" / "maketex";
    return { };
}

PackageLookup tlmgrLookup(fs::path cacheDir)
{
    return [cacheDir = std::move(cacheDir)](const std::string& file) -> std::optional<std::string> {
        using namespace std::chrono_literals;
        auto cacheFile = cacheDir.empty() ? fs::path { } : cacheDir / "packages.json";
        nlohmann::json cache = nlohmann::json::object();
        if (!cacheFile.empty()) {
            std::ifstream in(cacheFile);
            cache = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
            if (!cache.is_object())
                cache = nlohmann::json::object();
            if (auto it = cache.find(file); it != cache.end()) {
                if (it->is_string())
                    return it->get<std::string>();
                if (it->is_null())
                    return std::nullopt; // looked up before: no package has it
            }
        }

        // Avoid repeating an unreachable/timed-out tlmgr search for the same
        // file within one process (e.g. buildInstallingMissing followed by
        // addInstallHints when offline), without poisoning the on-disk cache.
        static std::mutex failedMutex;
        static std::set<std::pair<fs::path, std::string>> failedThisProcess;
        {
            std::lock_guard lock(failedMutex);
            if (failedThisProcess.contains({ cacheFile, file }))
                return std::nullopt;
        }

        // Output: a "tlmgr: package repository …" line, then each package
        // name on its own line ending in ':' followed by indented paths.
        // Anchor with '$' because --file takes a Perl regex: without it,
        // `/foo.sty` would match `/foo.sty.bak` or doc files first.
        auto out = run({ "tlmgr", "search", "--global", "--file", "/" + file + "$" }, 12s);
        if (!out) {
            std::lock_guard lock(failedMutex);
            failedThisProcess.emplace(cacheFile, file);
            return std::nullopt;
        }
        std::optional<std::string> package;
        std::string_view rest = *out;
        while (!rest.empty() && !package) {
            auto nl = rest.find('\n');
            auto line = rest.substr(0, nl);
            if (!line.empty() && line.back() == ':' && !line.starts_with("tlmgr")
                && !line.starts_with(' ') && !line.starts_with('\t'))
                package = std::string(line.substr(0, line.size() - 1));
            rest = nl == std::string_view::npos ? std::string_view { } : rest.substr(nl + 1);
        }
        // tlmgr answered (it was reachable): remember the result either way,
        // so a program no package provides (pygmentize) costs one network
        // round trip, not one per build. An unreachable tlmgr caches nothing.
        if (!cacheFile.empty()) {
            cache[file] = package ? nlohmann::json(*package) : nlohmann::json(nullptr);
            std::error_code ec;
            fs::create_directories(cacheDir, ec);
            std::ofstream(cacheFile) << cache.dump(2);
        }
        return package;
    };
}

bool isInstallable(const Diagnostic& d)
{
    if (d.missingFile.empty())
        return false;
    if (d.category == "missing-tool")
        return true;
    if (d.category != "missing-file")
        return false;
    // TikZ/PGF libraries ship as *.code.tex; regular .tex files are the
    // user's own \input sources and must not be looked up in CTAN.
    if (d.missingFile.ends_with(".code.tex"))
        return true;
    static constexpr std::array<std::string_view, 16> kInstallableExts = {
        ".sty", ".cls", ".ldf", ".bbx", ".cbx", ".lbx", ".dbx", ".bst",
        ".def", ".clo", ".cfg", ".fd", ".enc", ".map", ".tfm", ".otf"
    };
    auto ext = fs::path(d.missingFile).extension().string();
    return std::ranges::find(kInstallableExts, ext) != kInstallableExts.end();
}

void addInstallHints(std::vector<Diagnostic>& diagnostics)
{
    std::optional<TexDistribution> distro;
    std::optional<PackageLookup> lookup;
    for (auto& d : diagnostics) {
        // A missing \input'd .tex file is the user's, not a package.
        if (!d.hint.empty() || !isInstallable(d))
            continue;
        if (!distro) {
            distro = detectDistribution();
            lookup = *distro == TexDistribution::TexLive ? tlmgrLookup(userCacheDir())
                                                         : PackageLookup { };
        }
        d.hint = installHint(d.missingFile, *distro, *lookup);
    }
}

} // namespace maketex::cli
