#include "maketex/build/EngineMemory.h"

#include "maketex/StateDatabase.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <memory>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::build {

std::optional<EngineKind> loadDetectedEngine(const BuildOptions& options)
{
    fs::path dbPath = options.resolvedOutputDir() / ".maketex.db";
    std::error_code ec;
    if (!fs::is_regular_file(dbPath, ec))
        return std::nullopt;
    try {
        SQLite::Database db(dbPath.string(), SQLite::OPEN_READONLY);
        db.setBusyTimeout(10'000);
        SQLite::Statement q(db, "SELECT value FROM meta WHERE key = ?");
        q.bind(1, "job:" + jobNameFor(options) + ":detected_engine");
        if (!q.executeStep())
            return std::nullopt;
        return parseEngineKind(q.getColumn(0).getString());
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void saveDetectedEngine(const BuildOptions& options, EngineKind engine)
{
    fs::path dbPath = options.resolvedOutputDir() / ".maketex.db";
    try {
        auto db = std::make_unique<SQLite::Database>(
            dbPath.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        StateDatabase stateDb(std::move(db));
        stateDb.setJobMeta(jobNameFor(options), "detected_engine",
            std::string(engineKindName(engine)));
    } catch (const std::exception&) {
        // Best-effort cache; the build already succeeded.
    }
}

} // namespace maketex::build
