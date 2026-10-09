#include "maketex/graph/BuildGraph.h"

namespace maketex {

namespace fs = std::filesystem;

FileNode& BuildGraph::getOrAddFile(const fs::path& path, FileRole role)
{
    auto canonical = fs::weakly_canonical(path);
    auto key = canonical.string();
    auto it = m_files.find(key);
    if (it != m_files.end())
        return *it->second;

    auto node = std::make_unique<FileNode>(std::move(canonical), role);
    auto& ref = *node;
    m_files.emplace(std::move(key), std::move(node));
    return ref;
}

RuleNode& BuildGraph::getOrAddRule(const std::string& name)
{
    auto it = m_ruleIndex.find(name);
    if (it != m_ruleIndex.end())
        return *it->second;

    // Push the owning unique_ptr before indexing: if push_back throws, no
    // dangling pointer is committed to m_ruleIndex (strong exception guarantee).
    auto node = std::make_unique<RuleNode>(name);
    auto& ref = *node;
    m_rules.push_back(std::move(node));
    m_ruleIndex.emplace(name, &ref);
    return ref;
}

FileNode* BuildGraph::findFile(const fs::path& path)
{
    auto key = fs::weakly_canonical(path).string();
    auto it = m_files.find(key);
    return it != m_files.end() ? it->second.get() : nullptr;
}

RuleNode* BuildGraph::findRule(const std::string& name)
{
    auto it = m_ruleIndex.find(name);
    return it != m_ruleIndex.end() ? it->second : nullptr;
}

const FileNode* BuildGraph::findFile(const fs::path& path) const
{
    auto key = fs::weakly_canonical(path).string();
    auto it = m_files.find(key);
    return it != m_files.end() ? it->second.get() : nullptr;
}

const RuleNode* BuildGraph::findRule(const std::string& name) const
{
    auto it = m_ruleIndex.find(name);
    return it != m_ruleIndex.end() ? it->second : nullptr;
}

std::vector<FileNode*> BuildGraph::files()
{
    std::vector<FileNode*> result;
    result.reserve(m_files.size());
    for (const auto& [_, node] : m_files)
        result.push_back(node.get());
    return result;
}

std::vector<const FileNode*> BuildGraph::files() const
{
    std::vector<const FileNode*> result;
    result.reserve(m_files.size());
    for (const auto& [_, node] : m_files)
        result.push_back(node.get());
    return result;
}

} // namespace maketex
