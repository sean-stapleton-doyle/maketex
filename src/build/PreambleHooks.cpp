#include "maketex/build/PreambleHooks.h"

#include "maketex/DocumentProbe.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::build {

namespace {

    std::string detectMintedV2Hook(std::string_view relOutDir,
        const ProcessRunner& runner)
    {
        std::string sty;
        try {
            ProcessLaunchSpec spec {
                .argv = { "kpsewhich", "minted.sty" },
                .limits = { .timeout = std::chrono::seconds(5) },
            };
            auto r = runner ? runner(spec) : runSystemProcess(spec);
            sty = r.stdoutOutput.substr(0, r.stdoutOutput.find('\n'));
        } catch (const std::system_error&) {
        }
        std::ifstream in(sty);
        std::string head;
        std::string line;
        for (int i = 0; i < 60 && std::getline(in, line); ++i) {
            head += line;
            head += '\n';
        }
        if (auto major = parseMintedMajorVersion(head); major && *major < 3)
            return mintedOutputDirHook(relOutDir);
        return { };
    }

} // namespace

std::optional<int> parseMintedMajorVersion(std::string_view styHead)
{
    std::size_t start = 0;
    for (int i = 0; i < 60 && start < styHead.size(); ++i) {
        auto nl = styHead.find('\n', start);
        std::string_view line = nl == std::string_view::npos
            ? styHead.substr(start)
            : styHead.substr(start, nl - start);
        start = nl == std::string_view::npos ? styHead.size() : nl + 1;
        if (auto v = line.find(" v");
            line.contains("Yet another Pygments shim") && v != std::string_view::npos) {
            int major = 0;
            const char* first = line.data() + v + 2;
            const char* last = line.data() + line.size();
            auto [ptr, parseEc] = std::from_chars(first, last, major);
            if (parseEc == std::errc { })
                return major;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::string mintedOutputDirHook(std::string_view relOutDir)
{
    return "\\PassOptionsToPackage{outputdir=" + std::string(relOutDir) + "}{minted}";
}

std::string tikzExternalHook(std::string_view source,
    std::string_view engineBin, std::string_view relOutDir)
{
    if ((source.contains("\\tikzexternalize") || source.contains("\\usetikzlibrary{external}"))
        && !source.contains("system call")) {
        return "\\AtBeginDocument{\\ifdefined\\tikzset\\tikzset{external/system call={"
            + std::string(engineBin)
            + " \\tikzexternalcheckshellescape -halt-on-error -interaction=batchmode"
              " -output-directory=\""
            + std::string(relOutDir) + "\" -jobname \"\\image\" \"\\texsource\"}}\\fi}";
    }
    return { };
}

std::string buildPreambleHooks(const fs::path& texFile, const fs::path& outDir,
    EngineKind engineKind, std::string_view engineProgram, bool shellEscape,
    const ProcessRunner& runner)
{
    std::error_code ec;
    if (fs::equivalent(texFile.parent_path(), outDir, ec))
        return { };

    auto rel = fs::relative(outDir, texFile.parent_path(), ec).generic_string();
    if (ec || rel.empty() || rel == ".")
        return { };

    std::string preamble;
    auto req = analyzeRequirements(texFile);
    if (std::ranges::find(req.packages, "minted") != req.packages.end())
        preamble += detectMintedV2Hook(rel, runner);

    // TikZ externalization: pass -output-directory into the child engine
    // call when the user hasn't customized `system call` themselves.
    if (shellEscape) {
        std::ifstream in(texFile);
        std::string source((std::istreambuf_iterator<char>(in)), { });
        std::string bin = engineProgram.empty()
            ? std::string(engineKindName(engineKind))
            : std::string(engineProgram);
        preamble += tikzExternalHook(source, bin, rel);
    }
    return preamble;
}

} // namespace maketex::build
