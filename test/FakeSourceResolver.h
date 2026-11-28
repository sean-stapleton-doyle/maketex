#pragma once

#include "maketex/diagnostics/SourceResolver.h"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace maketex::test_utils {

// In-memory ISourceResolver for deterministic unit tests with no disk I/O.
class FakeSourceResolver final : public diagnostics::ISourceResolver {
public:
    void addFile(const std::filesystem::path& p, std::string content = { })
    {
        m_files[keyFor(p)] = std::move(content);
    }

    // Registers a path that reports isFile() == true but readText() == nullopt.
    void addUnreadableFile(const std::filesystem::path& p)
    {
        auto key = keyFor(p);
        m_files.erase(key);
        m_unreadable.insert(std::move(key));
    }

    // Maps an alias path (e.g. symlink) to its canonical target path.
    void addCanonicalAlias(const std::filesystem::path& from,
        const std::filesystem::path& to)
    {
        m_aliases[keyFor(from)] = keyFor(to);
    }

    bool isFile(const std::filesystem::path& p) const override
    {
        auto key = resolveKey(p);
        return m_files.contains(key) || m_unreadable.contains(key);
    }

    std::filesystem::path canonical(const std::filesystem::path& p) const override
    {
        return std::filesystem::path(resolveKey(p));
    }

    std::optional<std::string> readText(const std::filesystem::path& p) const override
    {
        auto key = resolveKey(p);
        if (auto it = m_files.find(key); it != m_files.end())
            return it->second;
        return std::nullopt;
    }

private:
    static std::string keyFor(const std::filesystem::path& p)
    {
        return p.lexically_normal().generic_string();
    }

    std::string resolveKey(const std::filesystem::path& p) const
    {
        auto key = keyFor(p);
        if (auto it = m_aliases.find(key); it != m_aliases.end())
            return it->second;
        return key;
    }

    std::map<std::string, std::string> m_files;
    std::set<std::string> m_unreadable;
    std::map<std::string, std::string> m_aliases;
};

} // namespace maketex::test_utils
