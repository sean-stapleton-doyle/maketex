#include "maketex/EngineBase.h"

#include "maketex/Launch.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace maketex {

RunResult<EngineInvocation> EngineBase::run(const EngineInvocation& inv,
    const WaitLimits& limits)
{
    // TeX wraps log/terminal lines at 79 columns by default, which splits
    // file paths and messages mid-token and defeats diagnostic parsing.
    // These web2c knobs (the same ones latexmk sets) keep lines whole;
    // error_line is capped at 254 by web2c itself.
    std::vector<std::pair<std::string, std::string>> env = {
        { "max_print_line", "10000" },
        { "error_line", "254" },
        { "half_error_line", "238" },
    };
    for (const auto& [k, v] : inv.extraEnv) {
        auto it = std::ranges::find(env, k, &std::pair<std::string, std::string>::first);
        if (it != env.end())
            it->second = v;
        else
            env.emplace_back(k, v);
    }

    // Run from the tex file's directory so that relative \input{} and \include{}
    // paths resolve correctly regardless of maketex's own working directory.
    return launchProcess(buildArgs(inv), inv.texFile.parent_path(),
        invokerKind(), inv, env, limits);
}

void EngineBase::appendExtraOptions(std::vector<std::string>& args) const
{
    auto key = [](std::string_view arg) { return arg.substr(0, arg.find('=')); };

    // Count, not iterator/span hoisted out of the loop: push_back below may
    // reallocate. Bounding the scan to the engine-built prefix is also what
    // lets repeated user options through.
    const std::size_t engineArgCount = args.size();
    for (const auto& opt : m_arguments.extraOptions) {
        bool clashes = std::ranges::any_of(std::span(args.data(), engineArgCount),
            [&](const std::string& existing) { return key(existing) == key(opt); });
        if (!clashes)
            args.push_back(opt);
    }
}

std::vector<std::string> EngineBase::texEngineArgs(std::string_view program,
    const EngineInvocation& inv) const
{
    std::vector<std::string> args;
    args.emplace_back(programOr(program));
    // -recorder is mandatory: the .fls it produces is the authoritative
    // dependency source for the fixed-point loop.
    args.emplace_back("-recorder");
    // nonstopmode: no interactive prompts, but errors are written to stdout
    // (and the .log file) so DiagnosticExtractor can parse them from the
    // captured output. batchmode would suppress all terminal output entirely.
    args.emplace_back("-interaction=nonstopmode");
    // file:line: error markers — the primary, unambiguous source for
    // DiagnosticExtractor's per-file error attribution.
    args.emplace_back("-file-line-error");
    if (inv.haltOnError)
        args.emplace_back("-halt-on-error");
    if (inv.shellEscape)
        args.emplace_back("-shell-escape");
    if (inv.synctex)
        args.emplace_back("-synctex=1");
    if (inv.draftMode)
        args.emplace_back(invokerKind() == InvokerKind::XeLaTeX ? "-no-pdf" : "-draftmode");
    if (!inv.formatName.empty())
        args.emplace_back("-fmt=" + inv.formatName);
    if (!inv.outputDir.empty())
        args.emplace_back("-output-directory=" + inv.outputDir.string());
    if (inv.jobName || !inv.preamble.empty() || inv.skipPreamble)
        // When the first input is TeX code rather than the file path, the job
        // name must be explicit.
        args.emplace_back("-jobname=" + inv.resolvedJobName());
    appendExtraOptions(args);
    if (inv.skipPreamble) {
        // Skip everything in texFile up to \begin{document} (already loaded in
        // the precompiled format) while keeping texFile as the active input
        // stream so -file-line-error reports exact file:line locations.
        std::string skip = inv.preamble
            + "\\makeatletter"
              "\\long\\def\\maketex@skip#1\\begin#2{"
              "\\def\\maketex@arg{#2}"
              "\\ifx\\maketex@arg\\maketex@doc\\expandafter\\maketex@done"
              "\\else\\expandafter\\maketex@skip\\fi}"
              "\\def\\maketex@doc{document}"
              "\\def\\maketex@done{\\makeatother\\begin{document}}"
              "\\expandafter\\maketex@skip\\@@input \""
            + inv.texFile.string() + "\"";
        args.emplace_back(std::move(skip));
    } else if (inv.preamble.empty()) {
        args.emplace_back(inv.texFile.string());
    } else {
        args.emplace_back(inv.preamble + "\\input{\"" + inv.texFile.string() + "\"}");
    }
    return args;
}

} // namespace maketex
