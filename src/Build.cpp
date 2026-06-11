#include "maketex/Build.h"

#include "maketex/Blake3Hasher.h"
#include "maketex/EngineInvocation.h"
#include "maketex/ITool.h"
#include "maketex/Process.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolFactory.h"
#include "maketex/engines/EngineFactory.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <memory>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace maketex {

std::vector<fs::path> texSystemRoots()
{
    std::vector<fs::path> roots;
    for (const char* var : { "TEXMFROOT", "TEXMFSYSVAR", "TEXMFVAR" }) {
        try {
            std::vector<std::string> args { "kpsewhich", "-var-value", var };
            auto result = Process(args).wait();
            if (result.exitCode != 0)
                continue;
            auto value = result.stdoutOutput;
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
                value.pop_back();
            if (!value.empty())
                roots.emplace_back(value);
        } catch (const std::system_error&) {
            break; // kpsewhich not installed — nothing to ignore
        }
    }
    return roots;
}

DriverResult runBuild(const BuildOptions& options)
{
    auto engine = createEngine(options.engine, options.engineArguments);

    fs::path outDir = options.outputDir.empty() ? options.texFile.parent_path()
                                                : options.outputDir;
    fs::path dbPath = outDir / ".maketex.db";

    auto db = std::make_unique<SQLite::Database>(
        dbPath.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    StateDatabase stateDb(std::move(db));

    Blake3Hasher hasher;

    ToolFactory factory(hasher);
    std::vector<std::unique_ptr<ITool>> tools;
    tools.reserve(options.tools.size());
    for (ToolKind kind : options.tools)
        tools.push_back(factory.create(kind));

    std::vector<ITool*> toolPtrs;
    toolPtrs.reserve(tools.size());
    for (auto& t : tools)
        toolPtrs.push_back(t.get());

    EngineInvocation inv {
        .texFile = options.texFile,
        .outputDir = outDir,
        .haltOnError = options.haltOnError,
        .shellEscape = options.shellEscape,
        .synctex = options.synctex,
    };

    BuildDriver driver(*engine, stateDb, hasher, inv, options.maxPasses,
        std::move(toolPtrs), texSystemRoots());
    return driver.run();
}

} // namespace maketex
