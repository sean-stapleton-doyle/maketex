#include "PackageInstaller.h"

#include "maketex/Process.h"

#include <algorithm>
#include <cerrno>
#include <format>
#include <iostream>
#include <set>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    constexpr int kMaxInstallRounds = 10;

    std::string joined(const std::vector<std::string>& items)
    {
        std::string out;
        for (const auto& item : items)
            out += (out.empty() ? "" : " ") + item;
        return out;
    }

    // Runs argv attached to this terminal (sudo must be able to prompt —
    // Process deliberately gives children /dev/null stdin). Returns the
    // exit code, or -1 when it could not be started.
    int runOnTerminal(const std::vector<std::string>& args)
    {
        std::vector<char*> argv;
        for (const auto& a : args)
            argv.push_back(const_cast<char*>(a.c_str())); // NOLINT — POSIX API
        argv.push_back(nullptr);
        pid_t pid;
        if (::posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
            return -1;
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    std::optional<fs::path> texmfRoot()
    {
        try {
            auto r = Process({ "kpsewhich", "-var-value=TEXMFROOT" }).wait({ .timeout = std::chrono::seconds(5) });
            auto root = r.stdoutOutput;
            while (!root.empty() && (root.back() == '\n' || root.back() == '\r'))
                root.pop_back();
            if (r.exitCode != 0 || root.empty())
                return std::nullopt;
            return fs::path(root);
        } catch (const std::system_error&) {
            return std::nullopt;
        }
    }

} // namespace

InstallCapability installCapability(TexDistribution distro, bool interactive)
{
    using Access = InstallCapability::Access;
    if (distro != TexDistribution::TexLive)
        return { Access::Unavailable,
            "automatic installs need TeX Live's tlmgr; this distribution installs packages "
            "through its own package manager" };
    auto root = texmfRoot();
    if (!root)
        return { Access::Unavailable, "kpsewhich could not locate the TeX Live tree" };
    // tlmgr writes its package database (tlpkg/) and the files (texmf-dist/).
    bool writable = ::access((*root / "tlpkg").c_str(), W_OK) == 0
        && ::access((*root / "texmf-dist").c_str(), W_OK) == 0;
    if (writable)
        return { Access::Direct, { } };
    if (interactive)
        return { Access::Sudo, std::format("{} is owned by root: tlmgr runs under sudo", root->string()) };
    return { Access::Unavailable,
        std::format("{} is not writable and maketex can't ask for a password here; run the "
                    "suggested command, or make the tree yours once: sudo chown -R $(whoami) {}",
            root->string(), root->string()) };
}

InstallPlan planInstall(const std::vector<Diagnostic>& diagnostics, const PackageLookup& lookup)
{
    InstallPlan plan;
    std::set<std::string> seen;
    for (const auto& d : diagnostics) {
        if (!isInstallable(d) || !seen.insert(d.missingFile).second)
            continue;
        if (auto package = packageProviding(d.missingFile, lookup)) {
            if (std::ranges::find(plan.packages, *package) == plan.packages.end())
                plan.packages.push_back(*package);
        } else {
            plan.unresolved.push_back(d.missingFile);
        }
    }
    return plan;
}

bool installPackages(const std::vector<std::string>& packages,
    InstallCapability::Access access, std::ostream& log)
{
    if (packages.empty() || access == InstallCapability::Access::Unavailable)
        return false;
    std::vector<std::string> args;
    if (access == InstallCapability::Access::Sudo)
        args.emplace_back("sudo");
    args.insert(args.end(), { "tlmgr", "install" });
    args.insert(args.end(), packages.begin(), packages.end());
    log << "maketex: installing " << joined(packages) << " ("
        << (access == InstallCapability::Access::Sudo ? "sudo " : "") << "tlmgr install)\n";
    log.flush();

    if (access == InstallCapability::Access::Sudo)
        return runOnTerminal(args) == 0;
    try {
        auto r = Process(args).wait({ .timeout = std::chrono::minutes(10) });
        if (r.exitCode != 0) {
            log << r.stdoutOutput << r.stderrOutput;
            return false;
        }
        return true;
    } catch (const std::system_error& e) {
        log << "maketex: cannot run tlmgr: " << e.what() << "\n";
        return false;
    }
}

bool askOnTerminal(const std::string& question)
{
    std::cerr << question << " [Y/n] " << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer))
        return false;
    return answer.empty() || answer.front() == 'y' || answer.front() == 'Y';
}

BuildResult buildInstallingMissing(const BuildOptions& options, const InstallPolicy& policy,
    std::ostream& log)
{
    std::vector<Diagnostic> notes;
    std::set<std::string> attempted;
    std::optional<TexDistribution> distro;
    std::optional<InstallCapability> capability;
    PackageLookup lookup;

    BuildResult result;
    for (int round = 0; round <= kMaxInstallRounds; ++round) {
        result = runBuild(options);
        if (result.success || round == kMaxInstallRounds)
            break;

        if (!distro) {
            distro = detectDistribution();
            if (*distro == TexDistribution::TexLive)
                lookup = tlmgrLookup(userCacheDir());
        }
        auto plan = planInstall(result.diagnostics, lookup);
        std::erase_if(plan.packages, [&](const std::string& p) { return attempted.contains(p); });
        if (plan.packages.empty())
            break;

        if (!capability)
            capability = installCapability(*distro, policy.interactive);
        if (capability->access == InstallCapability::Access::Unavailable) {
            if (policy.requested)
                notes.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Info,
                    .message = std::format("--install-missing could not install {}: {}",
                        joined(plan.packages), capability->reason),
                    .category = "install",
                });
            break;
        }
        // Installing by default must not spring a password prompt on
        // someone who never asked: say what is about to happen first.
        if (capability->access == InstallCapability::Access::Sudo && !policy.requested) {
            auto question = std::format("maketex: {} {} not installed. Install with `sudo tlmgr install {}`?",
                joined(plan.packages), plan.packages.size() == 1 ? "is" : "are", joined(plan.packages));
            if (!policy.confirm || !policy.confirm(question))
                break;
        }
        for (const auto& p : plan.packages)
            attempted.insert(p);
        if (!installPackages(plan.packages, capability->access, log)) {
            notes.push_back(Diagnostic {
                .severity = Diagnostic::Severity::Info,
                .message = std::format("tlmgr install {} failed (see its output above)", joined(plan.packages)),
                .category = "install",
            });
            break;
        }
        notes.push_back(Diagnostic {
            .severity = Diagnostic::Severity::Info,
            .message = std::format("installed {} with tlmgr", joined(plan.packages)),
            .category = "install",
        });
    }
    result.diagnostics.insert(result.diagnostics.begin(), notes.begin(), notes.end());
    return result;
}

} // namespace maketex::cli
