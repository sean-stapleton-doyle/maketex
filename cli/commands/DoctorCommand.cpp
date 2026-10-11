#include "commands/DoctorCommand.h"

#include "PackageHints.h"
#include "PackageInstaller.h"
#include "PathUtil.h"

#include "maketex/DocumentProbe.h"
#include "maketex/Process.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <set>
#include <string_view>
#include <unistd.h>

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    using namespace std::chrono_literals;

    // An auxiliary program maketex may run, and what for.
    struct ToolInfo {
        std::string_view program;
        std::string_view versionFlag; // empty: the program has none
        std::string_view purpose;
    };
    constexpr ToolInfo kTools[] = {
        { "bibtex", "--version", "\\bibliography documents" },
        { "biber", "--version", "biblatex documents" },
        { "makeindex", "", "indexes" },
        { "makeglossaries", "--version", "glossaries" },
        { "pythontex", "--version", "PythonTeX" },
        { "pygmentize", "-V", "minted code listings" },
    };

    std::string firstLine(std::string text)
    {
        auto start = text.find_first_not_of(" \n\r");
        if (start == std::string::npos)
            return { };
        text = text.substr(start);
        return text.substr(0, text.find_first_of("\r\n"));
    }

    // The first line `program flag` prints, or empty.
    std::string versionOf(const fs::path& program, std::string_view flag)
    {
        if (flag.empty())
            return { };
        try {
            auto r = Process({ program.string(), std::string(flag) }).wait({ .timeout = 5s });
            return firstLine(r.stdoutOutput.empty() ? r.stderrOutput : r.stdoutOutput);
        } catch (const std::system_error&) {
            return { };
        }
    }

    // Which of `files` kpsewhich finds — one process for the whole list.
    std::set<std::string> findTexFiles(const std::vector<std::string>& files)
    {
        std::set<std::string> found;
        if (files.empty())
            return found;
        std::vector<std::string> args { "kpsewhich" };
        args.insert(args.end(), files.begin(), files.end());
        try {
            auto r = Process(args).wait({ .timeout = 30s });
            std::string_view rest = r.stdoutOutput;
            while (!rest.empty()) {
                auto nl = rest.find('\n');
                auto line = rest.substr(0, nl);
                if (!line.empty())
                    found.insert(fs::path(line).filename().string());
                rest = nl == std::string_view::npos ? std::string_view { } : rest.substr(nl + 1);
            }
        } catch (const std::system_error&) {
        }
        return found;
    }

    std::string_view distributionName(TexDistribution d)
    {
        switch (d) {
        case TexDistribution::TexLive:
            return "TeX Live";
        case TexDistribution::Debian:
            return "TeX Live (Debian package)";
        case TexDistribution::Fedora:
            return "TeX Live (Fedora package)";
        case TexDistribution::MiKTeX:
            return "MiKTeX";
        case TexDistribution::Unknown:
            break;
        }
        return "unknown distribution";
    }

    // Prints check lines and counts the problems that matter.
    class Report {
    public:
        void section(std::string_view title) { std::cout << "\n"
                                                         << title << "\n"; }
        void ok(std::string_view what, std::string_view detail = { })
        {
            line("✓", what, detail);
        }
        void info(std::string_view what, std::string_view detail = { })
        {
            line("-", what, detail);
        }
        void warn(std::string_view what, std::string_view detail = { }, std::string_view hint = { })
        {
            line("!", what, detail);
            hintLine(hint);
            ++m_warnings;
        }
        void fail(std::string_view what, std::string_view detail = { }, std::string_view hint = { })
        {
            line("✗", what, detail);
            hintLine(hint);
            ++m_problems;
        }
        int problems() const { return m_problems; }
        int warnings() const { return m_warnings; }

    private:
        static void line(std::string_view mark, std::string_view what, std::string_view detail)
        {
            std::cout << "  " << mark << " " << what;
            if (!detail.empty())
                std::cout << "  " << detail;
            std::cout << "\n";
        }
        static void hintLine(std::string_view hint)
        {
            if (!hint.empty())
                std::cout << "      hint: " << hint << "\n";
        }
        int m_problems = 0;
        int m_warnings = 0;
    };

    // What a document lacks: TeX files kpsewhich can't find and programs
    // not on PATH, as missing-file/missing-tool diagnostics (the shape the
    // hint and install code consume).
    std::vector<Diagnostic> documentGaps(const DocumentRequirements& req)
    {
        std::vector<std::string> files;
        if (!req.documentClass.empty())
            files.push_back(req.documentClass + ".cls");
        for (const auto& p : req.packages)
            files.push_back(p + ".sty");
        auto found = findTexFiles(files);

        std::vector<Diagnostic> gaps;
        for (const auto& f : files)
            if (!found.contains(f))
                gaps.push_back({ .message = f, .category = "missing-file", .missingFile = f });
        for (const auto& [program, why] : req.programs)
            if (!findProgram(program))
                gaps.push_back({ .message = why, .category = "missing-tool", .missingFile = program });
        return gaps;
    }

} // namespace

int runDoctorCommand(const EffectiveOptions& options)
{
    Report report;
    const bool interactive = ::isatty(STDIN_FILENO) && ::isatty(STDERR_FILENO);

    // ── TeX installation ─────────────────────────────────────────────────
    report.section("TeX installation");
    auto distro = TexDistribution::Unknown;
    if (auto kpsewhich = findProgram("kpsewhich")) {
        distro = detectDistribution();
        report.ok(distributionName(distro), versionOf(*kpsewhich, "--version"));
        auto capability = installCapability(distro, interactive);
        switch (capability.access) {
        case InstallCapability::Access::Direct:
            report.ok("package installs", "tlmgr can install into the TeX tree; builds install missing packages");
            break;
        case InstallCapability::Access::Sudo:
            report.info("package installs",
                "builds ask before installing with sudo: " + capability.reason);
            break;
        case InstallCapability::Access::Unavailable:
            report.info("package installs", capability.reason);
            break;
        }
    } else {
        report.fail("no TeX installation on PATH", { },
            "install TeX Live (MacTeX or BasicTeX on macOS), or add its bin directory to PATH");
    }
    const PackageLookup lookup = distro == TexDistribution::TexLive ? tlmgrLookup(userCacheDir())
                                                                    : PackageLookup { };

    // ── Document requirements (computed first: they decide what's required)
    std::optional<DocumentRequirements> req;
    if (!options.build.texFile.empty())
        req = analyzeRequirements(options.build.texFile);
    auto engine = req ? chooseEngine(options.build) : EngineChoice { };

    // ── Engines ──────────────────────────────────────────────────────────
    report.section("Engines");
    int engines = 0;
    for (auto kind : { EngineKind::PdfLaTeX, EngineKind::XeLaTeX, EngineKind::LuaLaTeX }) {
        std::string name(engineKindName(kind));
        bool needed = req && engine.kind == kind;
        if (auto path = findProgram(name)) {
            ++engines;
            report.ok(name, versionOf(*path, "--version"));
        } else if (needed) {
            report.fail(name, "not installed — this document needs it",
                installHint(name, distro, lookup));
        } else {
            report.info(name, "not installed");
        }
    }
    if (engines == 0 && !req)
        report.fail("no TeX engine installed");

    // ── Tools ────────────────────────────────────────────────────────────
    report.section("Tools");
    for (const auto& tool : kTools) {
        std::string name(tool.program);
        auto need = req ? std::ranges::find(req->programs, name, &std::pair<std::string, std::string>::first)
                        : decltype(req->programs.end()) { };
        bool needed = req && need != req->programs.end();
        if (auto path = findProgram(name)) {
            auto version = versionOf(*path, tool.versionFlag);
            report.ok(name, version.empty() ? std::format("for {}", tool.purpose) : version);
        } else if (needed) {
            report.fail(name, std::format("not installed — this document {}", need->second),
                installHint(name, distro, lookup));
        } else {
            report.info(name, std::format("not installed (needed for {})", tool.purpose));
        }
    }

    // ── Document ─────────────────────────────────────────────────────────
    if (req) {
        report.section(std::format("Document {}", options.build.texFile.filename().string()));
        report.ok("engine",
            std::format("{}{}", engineKindName(engine.kind),
                options.build.engine        ? " (set by flag or config)"
                    : engine.reason.empty() ? " (default)"
                                            : " (" + engine.reason + ")"));

        auto gaps = documentGaps(*req);
        // doctor diagnoses; it installs only when asked explicitly.
        if (options.extras.installMissingRequested && !gaps.empty()) {
            auto plan = planInstall(gaps, lookup);
            auto capability = installCapability(distro, interactive);
            if (!plan.packages.empty() && capability.access != InstallCapability::Access::Unavailable
                && installPackages(plan.packages, capability.access, std::cerr))
                gaps = documentGaps(*req); // check again after installing
        }

        std::size_t texFiles = req->packages.size() + (req->documentClass.empty() ? 0 : 1);
        auto missingTexFiles = std::ranges::count(gaps, std::string("missing-file"), &Diagnostic::category);
        if (missingTexFiles == 0)
            report.ok("packages", std::format("class and {} package{} found", texFiles - 1, texFiles == 2 ? "" : "s"));
        // Programs are reported under Tools, which marks the ones this
        // document needs; here only its TeX files.
        for (const auto& gap : gaps)
            if (gap.category == "missing-file")
                report.fail(gap.missingFile, "not installed", installHint(gap.missingFile, distro, lookup));
        if (req->needsShellEscape && !options.build.shellEscape)
            report.warn("shell escape", "the document runs external programs (minted, svg)",
                "build with --shell-escape, or set `shell-escape = true` in maketex.toml "
                "(only for documents you trust)");
    } else {
        std::cout << "\n(pass a document — `maketex doctor thesis.tex` — to check its packages too)\n";
    }

    std::cout << "\n";
    if (report.problems() == 0)
        std::cout << "maketex doctor: everything needed is installed\n";
    else
        std::cout << std::format("maketex doctor: {} problem{} found\n", report.problems(),
            report.problems() == 1 ? "" : "s");
    return report.problems() == 0 ? kExitSuccess : kExitEnvironment;
}

} // namespace maketex::cli
