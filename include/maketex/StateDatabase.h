#pragma once

#include "maketex/PathCodec.h"

#include <SQLiteCpp/SQLiteCpp.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace maketex {

// 32-byte BLAKE3 digest
using Hash = std::array<uint8_t, 32>;

struct FileState {
    std::string path;
    bool exists = false;
    int64_t mtime = 0;
    int64_t size = 0;
    Hash hash = { };

    static FileState absent(std::string path)
    {
        return FileState { .path = std::move(path) };
    }
};

struct RuleRecord {
    std::string name;
    std::string command;
    int64_t ran_at; // unix timestamp, 0 if never run
    bool succeeded;
};

// Compatibility and portability settings for a state database.
struct StateDbConfig {
    // Paths under this root are stored relative, so a cache restored at a
    // different checkout path still hits. Empty stores paths verbatim.
    std::filesystem::path projectRoot;

    // Build-options fingerprint (engine, tools, flags). A stored database
    // whose fingerprint differs is wiped — its hashes were recorded under
    // different build semantics. Empty skips the comparison.
    std::string fingerprint;
};

// Wraps all SQL operations against the maketex state database.
// Takes ownership of the SQLite::Database — pass an in-memory database in
// tests, a file-backed one in production.
class StateDatabase {
public:
    // Bumped when the schema or the path encoding changes incompatibly;
    // databases written by other versions are wiped on open.
    static constexpr int kSchemaVersion = 2;

    // Applies the schema to db (idempotent — uses CREATE TABLE IF NOT
    // EXISTS), then drops all stored state when the database is from an
    // incompatible schema version or carries a different fingerprint.
    explicit StateDatabase(std::unique_ptr<SQLite::Database> db,
        const StateDbConfig& config = { });

    // True when incompatible prior state was dropped during construction —
    // the caller may want to tell the user why this build runs cold.
    bool wasReset() const { return m_wasReset; }

    // File state ──────────────────────────────────────────────────────────────

    std::optional<FileState> getFileState(const std::string& path) const;
    void upsertFileState(const FileState& state);

    // Batched upsert in a single transaction — used by the loop driver to
    // persist every tracked file's state at the end of a build so the next
    // invocation's (mtime, size) fast path can skip rehashing.
    void upsertFileStates(const std::vector<FileState>& states);

    // Rule records ────────────────────────────────────────────────────────────

    std::optional<RuleRecord> getRule(const std::string& name) const;
    void upsertRule(const RuleRecord& rule);

    // Rule input snapshots ────────────────────────────────────────────────────

    std::optional<Hash> getRuleInputHash(const std::string& ruleName,
        const std::string& filePath) const;

    void setRuleInputs(const std::string& ruleName,
        const std::vector<std::pair<std::string, Hash>>& inputs);

    std::vector<std::pair<std::string, Hash>> getRuleInputs(
        const std::string& ruleName) const;

private:
    void applySchema();
    void resetIncompatibleState(const std::string& fingerprint);
    std::optional<std::string> getMeta(const std::string& key) const;
    void setMeta(const std::string& key, const std::string& value);

    std::unique_ptr<SQLite::Database> m_db;
    PathCodec m_codec;
    bool m_wasReset = false;
};

} // namespace maketex
