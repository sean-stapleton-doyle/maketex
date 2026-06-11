#pragma once

#include <SQLiteCpp/SQLiteCpp.h>
#include <array>
#include <cstdint>
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

// Wraps all SQL operations against the maketex state database.
// Takes ownership of the SQLite::Database — pass an in-memory database in
// tests, a file-backed one in production.
class StateDatabase {
public:
    // Applies the schema to db (idempotent — uses CREATE TABLE IF NOT EXISTS).
    explicit StateDatabase(std::unique_ptr<SQLite::Database> db);

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

    std::unique_ptr<SQLite::Database> m_db;
};

} // namespace maketex
