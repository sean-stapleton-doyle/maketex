#include "maketex/build/PreambleFormat.h"

#include "maketex/parsers/FlsParser.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::build {

namespace {

    std::vector<std::pair<std::string, std::string>> collectPreambleInputDigests(
        const fs::path& projectRoot,
        const fs::path& fmtFls,
        const fs::path& preambleTex,
        IFileHasher& hasher,
        std::optional<std::span<const fs::path>> systemRoots)
    {
        std::vector<std::pair<std::string, std::string>> digests;
        std::error_code ec;
        if (!fs::is_regular_file(fmtFls, ec))
            return digests;

        std::vector<fs::path> roots = systemRoots
            ? std::vector<fs::path>(systemRoots->begin(), systemRoots->end())
            : texSystemRoots();
        auto rec = projectFlsParser(projectRoot, roots).parse(fmtFls);
        if (!rec)
            return digests;

        auto selfCanon = fs::weakly_canonical(preambleTex, ec);
        for (const auto& inp : rec->inputs) {
            if (fs::weakly_canonical(inp, ec) == selfCanon)
                continue;
            if (auto h = hasher.hashFile(inp, std::nullopt))
                digests.emplace_back(inp.string(), hexDigest(*h));
            else
                digests.emplace_back(inp.string(), "MISSING");
        }
        return digests;
    }

} // namespace

std::optional<std::string> extractDumpablePreambleText(std::string_view texSource)
{
    std::string preamble;
    bool foundBeginDoc = false;
    std::size_t start = 0;
    while (start < texSource.size()) {
        auto nl = texSource.find('\n', start);
        std::string_view line = nl == std::string_view::npos
            ? texSource.substr(start)
            : texSource.substr(start, nl - start);
        start = nl == std::string_view::npos ? texSource.size() : nl + 1;

        auto pct = line.find('%');
        auto code = line.substr(0, pct);
        auto pos = code.find("\\begin{document}");
        if (pos != std::string_view::npos) {
            preamble += code.substr(0, pos);
            foundBeginDoc = true;
            break;
        }
        if (code.contains("\\begin{"))
            return std::nullopt; // preamble uses \begin{...}; skip safely
        preamble += line;
        preamble += '\n';
    }
    if (!foundBeginDoc || preamble.empty())
        return std::nullopt;
    // minted runs shell-escape hooks during package load; skip .fmt cache.
    if (preamble.contains("{minted}"))
        return std::nullopt;
    return preamble;
}

std::optional<std::string> extractDumpablePreamble(const fs::path& texFile)
{
    std::ifstream in(texFile);
    if (!in)
        return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(in)), { });
    return extractDumpablePreambleText(content);
}

std::string hexDigest(const Hash& h)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(h.size() * 2);
    for (uint8_t b : h) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0f]);
    }
    return out;
}

std::string preambleCacheMaterial(std::string_view program,
    bool shellEscape,
    std::span<const std::string> extraOptions,
    std::span<const std::pair<std::string, std::string>> extraEnv,
    std::string_view preambleText,
    std::span<const std::pair<std::string, std::string>> inputDigests)
{
    std::string material = "fmt-v1\n";
    material += program;
    material += '\n';
    material += shellEscape ? "1\n" : "0\n";
    for (const auto& opt : extraOptions) {
        material += opt;
        material += '\n';
    }
    for (const auto& [k, v] : extraEnv) {
        material += k;
        material += '=';
        material += v;
        material += '\n';
    }
    material += "---\n";
    material += preambleText;
    material += "\n---\n";
    for (const auto& [path, digest] : inputDigests) {
        material += path;
        material += '=';
        material += digest;
        material += '\n';
    }
    return material;
}

std::vector<std::string> formatDumpArgs(std::string_view bin,
    const fs::path& outDir,
    std::string_view fmtName,
    bool shellEscape,
    const fs::path& preambleTex)
{
    std::string program = bin.empty() ? std::string("pdflatex") : std::string(bin);
    std::vector<std::string> args {
        std::move(program),
        "-ini",
        "-recorder",
        "-interaction=nonstopmode",
        "-halt-on-error",
        "-output-directory=" + outDir.string(),
        "-jobname=" + std::string(fmtName),
    };
    if (shellEscape)
        args.emplace_back("-shell-escape");
    args.emplace_back("&pdflatex");
    args.emplace_back("\\input{\"" + preambleTex.string() + "\"}");
    return args;
}

std::string preparePreambleFormat(const BuildOptions& options,
    const EngineArguments& engineArguments,
    const fs::path& outDir,
    const std::string& resolvedJob,
    const std::vector<std::pair<std::string, std::string>>& extraEnv,
    StateDatabase& stateDb,
    IFileHasher& hasher,
    const ProcessRunner& runner,
    std::optional<std::span<const fs::path>> systemRoots)
{
    auto preambleText = extractDumpablePreamble(options.texFile);
    if (!preambleText)
        return { };

    const std::string fmtName = resolvedJob + "-preamble";
    const fs::path fmtPath = outDir / (fmtName + ".fmt");
    const fs::path fmtFls = outDir / (fmtName + ".fls");
    const fs::path preambleTex = outDir / ("." + resolvedJob + "-preamble.tex");

    auto computeKey = [&] {
        auto digests = collectPreambleInputDigests(
            options.texFile.parent_path(), fmtFls, preambleTex, hasher, systemRoots);
        auto material = preambleCacheMaterial(engineArguments.program, options.shellEscape,
            engineArguments.extraOptions, extraEnv, *preambleText, digests);
        return hexDigest(hasher.hashBytes(material));
    };

    std::error_code ec;
    const std::string keyBefore = computeKey();
    if (fs::is_regular_file(fmtPath, ec) && fs::is_regular_file(fmtFls, ec)) {
        if (auto stored = stateDb.getJobMeta(resolvedJob, "preamble_fmt_key");
            stored && *stored == keyBefore)
            return fmtName;
    }

    // Write preamble wrapper + \dump and run `pdflatex -ini`.
    {
        std::ofstream out(preambleTex, std::ios::binary | std::ios::trunc);
        out << *preambleText << "\n\\dump\n";
    }
    ProcessLaunchSpec spec {
        .argv = formatDumpArgs(
            engineArguments.program, outDir, fmtName, options.shellEscape, preambleTex),
        .workingDir = options.texFile.parent_path(),
        .extraEnv = extraEnv,
        .limits = WaitLimits {
            .timeout = options.processTimeout,
            .stop = options.stopToken,
        },
    };

    try {
        auto r = runner ? runner(spec) : runSystemProcess(spec);
        if (r.exitCode != 0 || !fs::is_regular_file(fmtPath, ec)) {
            fs::remove(fmtPath, ec);
            return { };
        }
    } catch (const std::system_error&) {
        return { };
    }

    stateDb.setJobMeta(resolvedJob, "preamble_fmt_key", computeKey());
    return fmtName;
}

} // namespace maketex::build
