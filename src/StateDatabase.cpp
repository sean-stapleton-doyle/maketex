#include "maketex/StateDatabase.h"

#include <cstring>
#include <stdexcept>
#include <string_view>
#include <vector>

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

    CREATE TABLE IF NOT EXISTS meta (
        key   TEXT PRIMARY KEY NOT NULL,
        value TEXT NOT NULL
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

StateDatabase::StateDatabase(std::unique_ptr<SQLite::Database> db,
    const StateDbConfig& config)
    : m_db(std::move(db))
    , m_codec(config.projectRoot)
{
    m_db->exec("PRAGMA journal_mode=WAL");
    m_db->exec("PRAGMA foreign_keys=ON");
    applySchema();
    resetIncompatibleState(config.fingerprint, config.jobName);
}

void StateDatabase::applySchema()
{
    m_db->exec(k_schema);
}

// ── Manifest ──────────────────────────────────────────────────────────────────

std::optional<std::string> StateDatabase::getMeta(const std::string& key) const
{
    SQLite::Statement q(*m_db, "SELECT value FROM meta WHERE key = ?");
    q.bind(1, key);
    if (!q.executeStep())
        return std::nullopt;
    return q.getColumn(0).getString();
}

void StateDatabase::setMeta(const std::string& key, const std::string& value)
{
    SQLite::Statement s(*m_db,
        "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)");
    s.bind(1, key);
    s.bind(2, value);
    s.exec();
}

void StateDatabase::resetIncompatibleState(const std::string& fingerprint,
    const std::string& jobName)
{
    auto storedVersion = getMeta("schema_version");
    const std::string fingerprintKey = jobName.empty() ? "fingerprint" : "fingerprint:" + jobName;
    auto storedFingerprint = getMeta(fingerprintKey);

    bool schemaIncompatible = false;
    if (!storedVersion.has_value()) {
        // No manifest: either a brand-new database or one written before
        // the meta table existed (schema v1, absolute paths). The latter
        // has rows and must be wiped — its keys won't match this version's.
        SQLite::Statement q(*m_db,
            "SELECT EXISTS (SELECT 1 FROM file_states)"
            " OR EXISTS (SELECT 1 FROM rules)");
        schemaIncompatible = q.executeStep() && q.getColumn(0).getInt() != 0;
    } else {
        schemaIncompatible = *storedVersion != std::to_string(kSchemaVersion);
    }
    bool fingerprintChanged = !fingerprint.empty() && storedFingerprint.has_value()
        && *storedFingerprint != fingerprint;

    SQLite::Transaction tx(*m_db);
    // Never try to migrate: wrong state silently corrupts incremental
    // decisions, while an empty database merely costs one full build.
    if (schemaIncompatible) {
        m_db->exec("DELETE FROM rule_inputs;"
                   "DELETE FROM rules;"
                   "DELETE FROM file_states;"
                   "DELETE FROM meta;");
        m_wasReset = true;
    } else if (fingerprintChanged) {
        // Only this job's rules: every rule name ends in ":<job>", plus a
        // "#…" suffix for derived records. file_states is a pure content
        // cache and stays valid.
        if (jobName.empty()) {
            m_db->exec("DELETE FROM rule_inputs; DELETE FROM rules;");
        } else {
            std::vector<std::string> doomed;
            SQLite::Statement q(*m_db, "SELECT name FROM rules");
            const std::string suffix = ":" + jobName;
            while (q.executeStep()) {
                std::string name = q.getColumn(0).getString();
                std::string_view base = name;
                base = base.substr(0, base.find('#'));
                if (base.ends_with(suffix))
                    doomed.push_back(std::move(name));
            }
            SQLite::Statement del(*m_db, "DELETE FROM rules WHERE name = ?");
            for (const auto& name : doomed) {
                del.bind(1, name);
                del.exec();
                del.reset();
            }
        }
        m_wasReset = true;
    }
    setMeta("schema_version", std::to_string(kSchemaVersion));
    if (!fingerprint.empty() || !storedFingerprint.has_value())
        setMeta(fingerprintKey, fingerprint);
#ifdef MAKETEX_VERSION
    setMeta("maketex_version", MAKETEX_VERSION); // informational only
#endif
    tx.commit();
}

// ── File state ────────────────────────────────────────────────────────────────

std::optional<FileState> StateDatabase::getFileState(const std::string& path) const
{
    SQLite::Statement q(*m_db,
        "SELECT is_present, mtime, size, hash FROM file_states WHERE path = ?");
    q.bind(1, m_codec.encode(path));
    if (!q.executeStep())
        return std::nullopt;

    // Callers think in absolute paths; the encoded form is storage-only.
    if (q.getColumn(0).getInt() == 0)
        return FileState::absent(path);

    return FileState {
        .path = path,
        .exists = true,
        .mtime = q.getColumn(1).getInt64(),
        .size = q.getColumn(2).getInt64(),
        .hash = columnToHash(q.getColumn(3)),
    };
}

void StateDatabase::upsertFileState(const FileState& state)
{
    SQLite::Statement s(*m_db,
        "INSERT OR REPLACE INTO file_states (path, is_present, mtime, size, hash)"
        " VALUES (?, ?, ?, ?, ?)");
    s.bind(1, m_codec.encode(state.path));
    s.bind(2, static_cast<int>(state.exists));
    s.bind(3, state.mtime);
    s.bind(4, state.size);
    s.bind(5, state.hash.data(), static_cast<int>(state.hash.size()));
    s.exec();
}

void StateDatabase::upsertFileStates(const std::vector<FileState>& states)
{
    SQLite::Transaction tx(*m_db);
    for (const auto& state : states)
        upsertFileState(state);
    tx.commit();
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
    q.bind(2, m_codec.encode(filePath));
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
        ins.bind(2, m_codec.encode(path));
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
        result.emplace_back(m_codec.decode(q.getColumn(0).getString()).string(),
            columnToHash(q.getColumn(1)));

    return result;
}

} // namespace maketex
