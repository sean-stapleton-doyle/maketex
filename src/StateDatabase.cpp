#include "StateDatabase.h"

#include <cstring>
#include <stdexcept>

namespace maketex {

namespace {

    constexpr auto k_schema = R"sql(
    CREATE TABLE IF NOT EXISTS file_states (
        path       TEXT    PRIMARY KEY NOT NULL,
        is_present INTEGER NOT NULL DEFAULT 1,
        mtime      INTEGER NOT NULL DEFAULT 0,
        size       INTEGER NOT NULL DEFAULT 0,
        hash       BLOB    NOT NULL
    );

    CREATE TABLE IF NOT EXISTS rules (
        name      TEXT    PRIMARY KEY,
        command   TEXT    NOT NULL,
        ran_at    INTEGER NOT NULL DEFAULT 0,
        succeeded INTEGER NOT NULL DEFAULT 0
    );

    CREATE TABLE IF NOT EXISTS rule_inputs (
        rule_name   TEXT NOT NULL REFERENCES rules(name) ON DELETE CASCADE,
        file_path   TEXT NOT NULL,
        hash_at_run BLOB NOT NULL,
        PRIMARY KEY (rule_name, file_path)
    );
)sql";

    Hash columnToHash(const SQLite::Column& col)
    {
        if (col.getBytes() != static_cast<int>(sizeof(Hash)))
            throw std::runtime_error("Corrupt hash blob: unexpected size");
        Hash h;
        std::memcpy(h.data(), col.getBlob(), sizeof(Hash));
        return h;
    }

} // namespace

StateDatabase::StateDatabase(std::unique_ptr<SQLite::Database> db)
    : m_db(std::move(db))
{
    m_db->exec("PRAGMA journal_mode=WAL");
    m_db->exec("PRAGMA foreign_keys=ON");
    applySchema();
}

void StateDatabase::applySchema()
{
    m_db->exec(k_schema);
}

// ── File state ────────────────────────────────────────────────────────────────

std::optional<FileState> StateDatabase::getFileState(const std::string& path) const
{
    SQLite::Statement q(*m_db,
        "SELECT path, is_present, mtime, size, hash FROM file_states WHERE path = ?");
    q.bind(1, path);
    if (!q.executeStep())
        return std::nullopt;

    if (q.getColumn(1).getInt() == 0)
        return FileState::absent(q.getColumn(0).getString());

    return FileState {
        .path = q.getColumn(0).getString(),
        .exists = true,
        .mtime = q.getColumn(2).getInt64(),
        .size = q.getColumn(3).getInt64(),
        .hash = columnToHash(q.getColumn(4)),
    };
}

void StateDatabase::upsertFileState(const FileState& state)
{
    SQLite::Statement s(*m_db,
        "INSERT OR REPLACE INTO file_states (path, is_present, mtime, size, hash)"
        " VALUES (?, ?, ?, ?, ?)");
    s.bind(1, state.path);
    s.bind(2, static_cast<int>(state.exists));
    s.bind(3, state.mtime);
    s.bind(4, state.size);
    s.bind(5, state.hash.data(), static_cast<int>(state.hash.size()));
    s.exec();
}

// ── Rule records ──────────────────────────────────────────────────────────────

std::optional<RuleRecord> StateDatabase::getRule(const std::string& name) const
{
    SQLite::Statement q(*m_db,
        "SELECT name, command, ran_at, succeeded FROM rules WHERE name = ?");
    q.bind(1, name);
    if (!q.executeStep())
        return std::nullopt;

    return RuleRecord {
        .name = q.getColumn(0).getString(),
        .command = q.getColumn(1).getString(),
        .ran_at = q.getColumn(2).getInt64(),
        .succeeded = q.getColumn(3).getInt() != 0,
    };
}

void StateDatabase::upsertRule(const RuleRecord& rule)
{
    SQLite::Statement s(*m_db,
        "INSERT OR REPLACE INTO rules (name, command, ran_at, succeeded)"
        " VALUES (?, ?, ?, ?)");
    s.bind(1, rule.name);
    s.bind(2, rule.command);
    s.bind(3, rule.ran_at);
    s.bind(4, static_cast<int>(rule.succeeded));
    s.exec();
}

// ── Rule input snapshots ──────────────────────────────────────────────────────

std::optional<Hash> StateDatabase::getRuleInputHash(const std::string& ruleName,
    const std::string& filePath) const
{
    SQLite::Statement q(*m_db,
        "SELECT hash_at_run FROM rule_inputs"
        " WHERE rule_name = ? AND file_path = ?");
    q.bind(1, ruleName);
    q.bind(2, filePath);
    if (!q.executeStep())
        return std::nullopt;

    return columnToHash(q.getColumn(0));
}

void StateDatabase::setRuleInputs(const std::string& ruleName,
    const std::vector<std::pair<std::string, Hash>>& inputs)
{
    SQLite::Transaction tx(*m_db);

    SQLite::Statement del(*m_db,
        "DELETE FROM rule_inputs WHERE rule_name = ?");
    del.bind(1, ruleName);
    del.exec();

    SQLite::Statement ins(*m_db,
        "INSERT INTO rule_inputs (rule_name, file_path, hash_at_run)"
        " VALUES (?, ?, ?)");
    for (const auto& [path, hash] : inputs) {
        ins.bind(1, ruleName);
        ins.bind(2, path);
        ins.bind(3, hash.data(), static_cast<int>(hash.size()));
        ins.exec();
        ins.reset();
    }

    tx.commit();
}

std::vector<std::pair<std::string, Hash>> StateDatabase::getRuleInputs(
    const std::string& ruleName) const
{
    SQLite::Statement q(*m_db,
        "SELECT file_path, hash_at_run FROM rule_inputs WHERE rule_name = ?");
    q.bind(1, ruleName);

    std::vector<std::pair<std::string, Hash>> result;
    while (q.executeStep())
        result.emplace_back(q.getColumn(0).getString(), columnToHash(q.getColumn(1)));

    return result;
}

} // namespace maketex
