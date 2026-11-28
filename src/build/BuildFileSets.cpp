#include "maketex/build/BuildFileSets.h"

#include "maketex/Build.h"
#include "maketex/EngineInvocation.h"
#include "maketex/ITool.h"
#include "maketex/ToolContext.h"
#include "maketex/ToolFactory.h"
#include "maketex/build/Environment.h"
#include "maketex/build/JobName.h"
#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/LogParser.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;

namespace maketex::build {

bool isUnderRoot(const fs::path& p, const fs::path& root)
{
    auto [a, b] = std::ranges::mismatch(root, p);
    return a == root.end();
}

std::vector<fs::path> sourcesFromFls(const FlsRecord& record)
{
    std::unordered_set<std::string> outputPaths;
    for (const auto& p : record.outputs)
        outputPaths.insert(p.string());

    std::vector<fs::path> sources;
    std::unordered_set<std::string> seenSources;
    for (const auto& p : record.inputs) {
        if (!outputPaths.contains(p.string()) && seenSources.insert(p.string()).second)
            sources.push_back(p);
    }
    return sources;
}

} // namespace maketex::build

namespace maketex {

namespace {

    fs::path safeCanonical(const fs::path& p)
    {
        std::error_code ec;
        auto c = fs::weakly_canonical(p, ec);
        return ec ? p.lexically_normal() : c;
    }

    void appendPreambleFlsSources(const EngineInvocation& inv,
        const FlsParser& flsParser,
        const fs::path& outDirCanonical,
        const std::unordered_set<std::string>& outputPaths,
        std::unordered_set<std::string>& seenSources,
        std::vector<fs::path>& sources)
    {
        // When a precompiled preamble format was used, local files loaded inside
        // the preamble (e.g. shared/preamble.tex, custom.cls) are recorded in
        // `<job>-preamble.fls` rather than `<job>.fls`. Include them as sources so
        // `maketex watch` rebuilds when preamble files change.
        if (auto fmtFls = inv.outputDir / (inv.resolvedJobName() + "-preamble.fls");
            fs::exists(fmtFls)) {
            if (auto fmtRec = flsParser.parse(fmtFls)) {
                for (const auto& p : fmtRec->inputs) {
                    auto canon = safeCanonical(p);
                    if (outputPaths.contains(canon.string())
                        || build::isUnderRoot(canon, outDirCanonical))
                        continue;
                    if (seenSources.insert(canon.string()).second)
                        sources.push_back(std::move(canon));
                }
            }
        }
    }

    void appendToolSources(const BuildOptions& options,
        const EngineInvocation& inv,
        const FlsRecord& record,
        const fs::path& outDirCanonical,
        const std::vector<fs::path>& systemRootsCanonical,
        const std::unordered_set<std::string>& outputPaths,
        std::unordered_set<std::string>& seenSources,
        std::vector<fs::path>& sources)
    {
        auto logFile = inv.jobFile(".log");
        auto auxFile = inv.jobFile(".aux");
        ToolContext ctx(
            inv,
            [logFile] { return LogParser { }.parse(logFile).value_or(LogRecord { }); },
            [auxFile] {
                std::error_code ec;
                if (!fs::exists(auxFile, ec))
                    return AuxRecord { };
                return AuxParser { }.parse(auxFile).value_or(AuxRecord { });
            },
            [record] { return record; });

        for (ToolKind kind : options.tools) {
            auto tool = createTool(kind);
            if (!tool->applies(ctx))
                continue;
            for (const auto& input : tool->inputs(ctx)) {
                if (input.content.has_value())
                    continue;
                std::error_code ec;
                if (!fs::is_regular_file(input.path, ec))
                    continue;
                auto canon = safeCanonical(input.path);
                if (outputPaths.contains(canon.string())
                    || build::isUnderRoot(canon, outDirCanonical))
                    continue;
                bool inSystem = std::ranges::any_of(systemRootsCanonical,
                    [&](const fs::path& root) { return build::isUnderRoot(canon, root); });
                if (inSystem)
                    continue;
                if (seenSources.insert(canon.string()).second)
                    sources.push_back(std::move(canon));
            }
        }
    }

} // namespace

std::optional<BuildFileSets> scanBuildFiles(const BuildOptions& options)
{
    auto flagJobName = build::splitJobName(options.engineArguments).second;
    EngineInvocation inv {
        .texFile = options.texFile,
        .outputDir = options.resolvedOutputDir(),
        .jobName = options.jobName ? options.jobName : flagJobName,
        .extraEnv = build::searchPathEnv(options),
    };
    fs::path flsPath = inv.jobFile(".fls");
    auto systemRoots = texSystemRoots();
    auto flsParser = projectFlsParser(options.texFile.parent_path(), systemRoots);

    auto record = flsParser.parse(flsPath);
    if (!record)
        return std::nullopt;

    std::unordered_set<std::string> outputPaths;
    for (const auto& p : record->outputs)
        outputPaths.insert(p.string());

    BuildFileSets sets;
    sets.sources = build::sourcesFromFls(*record);
    std::unordered_set<std::string> seenSources;
    for (const auto& p : sets.sources)
        seenSources.insert(p.string());

    const fs::path outDirCanonical = safeCanonical(inv.outputDir);
    std::vector<fs::path> systemRootsCanonical;
    systemRootsCanonical.reserve(systemRoots.size());
    for (const auto& r : systemRoots)
        systemRootsCanonical.push_back(safeCanonical(r));

    appendPreambleFlsSources(
        inv, flsParser, outDirCanonical, outputPaths, seenSources, sets.sources);
    // Auxiliary tools read files the TeX engine never opens (.bib databases,
    // local .bst styles), so they are absent from the .fls. Include them as
    // sources so `maketex watch` rebuilds when a bibliography is edited.
    appendToolSources(options, inv, *record, outDirCanonical, systemRootsCanonical,
        outputPaths, seenSources, sets.sources);

    sets.generated = std::move(record->outputs);
    return sets;
}

} // namespace maketex
