#include "commands/DoctorCommand.h"

#include "PackageHints.h"
#include "PackageInstaller.h"
#include "PathUtil.h"

#include "maketex/DocumentProbe.h"
#include "maketex/Process.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
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
        { "makeindex", "", "indexes and nomenclature" },
        { "makeglossaries", "--version", "glossaries" },
        { "bib2gls", "--version", "glossaries-extra (bib2gls)" },
        { "pythontex", "--version", "PythonTeX" },
        { "pygmentize", "-V", "minted code listings" },
        { "inkscape", "--version", "svg figures" },
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
            auto r = Process(std::vector<std::string> { program.string(), std::string(flag) }).wait({ .timeout = 5s });
            return firstLine(r.stdoutOutput.empty() ? r.stderrOutput : r.stdoutOutput);
        } catch (const std::system_error&) {
            return { };
        }
    }

    // Which of `files` kpsewhich finds — one process for the whole list.
    // Runs from `workingDir` (the document's directory) with any configured
    // texinputs prepended to TEXINPUTS so local/shared .cls/.sty files are
    // found just as the engine will find them.
    std::set<std::string> kpsewhichFindTexFiles(const std::vector<std::string>& files,
        const fs::path& workingDir, const std::vector<std::string>& texInputs)
    {
        std::set<std::string> found;
        if (files.empty())
            return found;
        std::vector<std::string> args { "kpsewhich" };
        args.insert(args.end(), files.begin(), files.end());
        std::vector<std::pair<std::string, std::string>> env;
        if (!texInputs.empty()) {
            std::string value;
            for (const auto& d : texInputs) {
                value += d;
                value += ':';
            }
            if (const char* existing = std::getenv("TEXINPUTS"); existing && *existing)
                value += existing;
            env.emplace_back("TEXINPUTS", std::move(value));
        }
        try {
            auto r = Process(args, workingDir, env).wait({ .timeout = 30s });
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

    class SystemDoctorEnv final : public IDoctorEnv {
    public:
        std::optional<fs::path> findProgram(const std::string& name) override
        {
            return maketex::cli::findProgram(name);
        }

        std::string programVersion(const fs::path& program, std::string_view flag) override
        {
            return versionOf(program, flag);
        }

        TexDistribution detectDistribution() override
        {
            return maketex::cli::detectDistribution();
        }

        InstallCapability installCapability(TexDistribution distro, bool interactive) override
        {
            return maketex::cli::installCapability(distro, interactive);
        }

        PackageLookup packageLookup(TexDistribution distro) override
        {
            return distro == TexDistribution::TexLive ? tlmgrLookup(userCacheDir())
                                                      : PackageLookup { };
        }

        DocumentRequirements analyzeRequirements(const fs::path& texFile) override
        {
            return maketex::analyzeRequirements(texFile);
        }

        EngineChoice chooseEngine(const BuildOptions& build) override
        {
            return maketex::chooseEngine(build);
        }

        std::set<std::string> findTexFiles(const std::vector<std::string>& files,
            const fs::path& workingDir, const std::vector<std::string>& texInputs) override
        {
            return kpsewhichFindTexFiles(files, workingDir, texInputs);
        }

        bool installPackages(const std::vector<std::string>& packages,
            InstallCapability::Access access, std::ostream& log) override
        {
            return maketex::cli::installPackages(packages, access, log);
        }

        bool isInteractive() const override
        {
            return ::isatty(STDIN_FILENO) == 1 && ::isatty(STDERR_FILENO) == 1;
        }

        std::ostream& out() override { return std::cout; }
        std::ostream& err() override { return std::cerr; }
    };

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
        explicit Report(std::ostream& out)
            : m_out(out)
        {
        }

        void section(std::string_view title) { m_out << "\n"
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
        void line(std::string_view mark, std::string_view what, std::string_view detail)
        {
            m_out << "  " << mark << " " << what;
            if (!detail.empty())
                m_out << "  " << detail;
            m_out << "\n";
        }
        void hintLine(std::string_view hint)
        {
            if (!hint.empty())
                m_out << "      hint: " << hint << "\n";
        }

        std::ostream& m_out;
        int m_problems = 0;
        int m_warnings = 0;
    };

    // What a document lacks: TeX files kpsewhich can't find and programs
    // not on PATH, as missing-file/missing-tool diagnostics (the shape the
    // hint and install code consume).
    std::vector<Diagnostic> documentGaps(const DocumentRequirements& req,
        const fs::path& docDir, const EngineChoice& engine,
        const std::vector<std::string>& texInputs, IDoctorEnv& env)
    {
        std::vector<std::string> files;
        if (!req.documentClass.empty())
            files.push_back(req.documentClass + ".cls");
        for (const auto& p : req.packages)
            files.push_back(p + ".sty");
        auto found = env.findTexFiles(files, docDir, texInputs);

        std::vector<Diagnostic> gaps;
        std::string engineProg(engineKindName(engine.kind));
        if (!env.findProgram(engineProg))
            gaps.push_back({ .message = "engine", .category = "missing-tool", .missingFile = engineProg });
        for (const auto& f : files)
            if (!found.contains(f))
                gaps.push_back({ .message = f, .category = "missing-file", .missingFile = f });
        for (const auto& [program, why] : req.programs)
            if (!env.findProgram(program))
                gaps.push_back({ .message = why, .category = "missing-tool", .missingFile = program });
        return gaps;
    }

    TexDistribution checkTexInstallation(Report& report, IDoctorEnv& env, bool interactive)
    {
        report.section("TeX installation");
        auto distro = TexDistribution::Unknown;
        if (auto kpsewhich = env.findProgram("kpsewhich")) {
            distro = env.detectDistribution();
            report.ok(distributionName(distro), env.programVersion(*kpsewhich, "--version"));
            auto capability = env.installCapability(distro, interactive);
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
        return distro;
    }

    void maybeInstallMissing(const EffectiveOptions& options, const DocumentRequirements& req,
        const fs::path& docDir, const EngineChoice& engine, TexDistribution distro,
        const PackageLookup& lookup, bool interactive, IDoctorEnv& env)
    {
        auto gaps = documentGaps(req, docDir, engine, options.build.searchPaths.texInputs, env);
        if (gaps.empty())
            return;
        auto plan = planInstall(gaps, lookup);
        auto capability = env.installCapability(distro, interactive);
        if (!plan.packages.empty() && capability.access != InstallCapability::Access::Unavailable)
            env.installPackages(plan.packages, capability.access, env.err());
    }

    void checkEngines(Report& report, IDoctorEnv& env,
        const std::optional<DocumentRequirements>& req, const EngineChoice& engine,
        TexDistribution distro, const PackageLookup& lookup)
    {
        report.section("Engines");
        int engines = 0;
        for (auto kind : { EngineKind::PdfLaTeX, EngineKind::XeLaTeX, EngineKind::LuaLaTeX }) {
            std::string name(engineKindName(kind));
            bool needed = req && engine.kind == kind;
            if (auto path = env.findProgram(name)) {
                ++engines;
                report.ok(name, env.programVersion(*path, "--version"));
            } else if (needed) {
                report.fail(name, "not installed — this document needs it",
                    installHint(name, distro, lookup));
            } else {
                report.info(name, "not installed");
            }
        }
        if (engines == 0 && !req)
            report.fail("no TeX engine installed");
    }

    void checkTools(Report& report, IDoctorEnv& env,
        const std::optional<DocumentRequirements>& req,
        TexDistribution distro, const PackageLookup& lookup)
    {
        report.section("Tools");
        for (const auto& tool : kTools) {
            std::string name(tool.program);
            auto need = req ? std::ranges::find(req->programs, name, &std::pair<std::string, std::string>::first)
                            : decltype(req->programs.end()) { };
            bool needed = req && need != req->programs.end();
            if (auto path = env.findProgram(name)) {
                auto version = env.programVersion(*path, tool.versionFlag);
                report.ok(name, version.empty() ? std::format("for {}", tool.purpose) : version);
            } else if (needed) {
                report.fail(name, std::format("not installed — this document {}", need->second),
                    installHint(name, distro, lookup));
            } else {
                report.info(name, std::format("not installed (needed for {})", tool.purpose));
            }
        }
    }

    void checkDocument(Report& report, IDoctorEnv& env, const EffectiveOptions& options,
        const DocumentRequirements& req, const fs::path& docDir,
        const EngineChoice& engine, TexDistribution distro, const PackageLookup& lookup)
    {
        report.section(std::format("Document {}", options.build.texFile.filename().string()));
        report.ok("engine",
            std::format("{}{}", engineKindName(engine.kind),
                options.build.engine        ? " (set by flag or config)"
                    : engine.reason.empty() ? " (default)"
                                            : " (" + engine.reason + ")"));

        auto gaps = documentGaps(req, docDir, engine, options.build.searchPaths.texInputs, env);

        const std::size_t pkgCount = req.packages.size();
        const bool hasClass = !req.documentClass.empty();
        auto missingTexFiles = std::ranges::count(gaps, std::string("missing-file"), &Diagnostic::category);
        if (missingTexFiles == 0) {
            if (hasClass)
                report.ok("packages", std::format("class and {} package{} found", pkgCount, pkgCount == 1 ? "" : "s"));
            else
                report.ok("packages", std::format("{} package{} found", pkgCount, pkgCount == 1 ? "" : "s"));
        }
        // Programs are reported under Tools, which marks the ones this
        // document needs; here only its TeX files.
        for (const auto& gap : gaps)
            if (gap.category == "missing-file")
                report.fail(gap.missingFile, "not installed", installHint(gap.missingFile, distro, lookup));
        if (req.needsShellEscape && !options.build.shellEscape)
            report.warn("shell escape", "the document runs external programs (minted, svg, tikz externalize)",
                "build with --shell-escape, or set `shell-escape = true` in maketex.toml "
                "(only for documents you trust)");
    }

} // namespace

int runDoctorCommand(const EffectiveOptions& options, IDoctorEnv& env)
{
    Report report(env.out());
    const bool interactive = env.isInteractive();

    const TexDistribution distro = checkTexInstallation(report, env, interactive);
    const PackageLookup lookup = env.packageLookup(distro);

    // Document requirements (computed first: they decide what's required).
    std::optional<DocumentRequirements> req;
    if (!options.build.texFile.empty())
        req = env.analyzeRequirements(options.build.texFile);
    auto engine = req ? env.chooseEngine(options.build) : EngineChoice { };
    const fs::path docDir = options.build.texFile.empty()
        ? fs::current_path()
        : options.build.texFile.parent_path();

    // doctor diagnoses by default; when --install-missing is given explicitly,
    // install missing engines, tools, and packages before reporting so every
    // section reflects the final state of the system.
    if (req && options.extras.installMissingRequested)
        maybeInstallMissing(options, *req, docDir, engine, distro, lookup, interactive, env);

    checkEngines(report, env, req, engine, distro, lookup);
    checkTools(report, env, req, distro, lookup);

    if (req) {
        checkDocument(report, env, options, *req, docDir, engine, distro, lookup);
    } else {
        env.out() << "\n(pass a document — `maketex doctor thesis.tex` — to check its packages too)\n";
    }

    env.out() << "\n";
    if (report.problems() == 0)
        env.out() << "maketex doctor: everything needed is installed\n";
    else
        env.out() << std::format("maketex doctor: {} problem{} found\n", report.problems(),
            report.problems() == 1 ? "" : "s");
    return report.problems() == 0 ? kExitSuccess : kExitEnvironment;
}

int runDoctorCommand(const EffectiveOptions& options)
{
    SystemDoctorEnv env;
    return runDoctorCommand(options, env);
}

} // namespace maketex::cli
