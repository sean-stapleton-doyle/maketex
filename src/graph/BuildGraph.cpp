#include "maketex/graph/BuildGraph.h"

namespace maketex {

namespace fs = std::filesystem;

FileNode& BuildGraph::getOrAddFile(fs::path path, FileRole role)
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

RuleNode& BuildGraph::getOrAddRule(std::string name)
{
    auto it = m_ruleIndex.find(name);
    if (it != m_ruleIndex.end())
        return *it->second;

    auto node = std::make_unique<RuleNode>(name);
    auto* ptr = node.get();
    m_ruleIndex.emplace(name, ptr);
    m_rules.push_back(std::move(node));
    return *ptr;
}

FileNode* BuildGraph::findFile(const fs::path& path) const
{
    auto key = fs::weakly_canonical(path).string();
    auto it = m_files.find(key);
    return it != m_files.end() ? it->second.get() : nullptr;
}

RuleNode* BuildGraph::findRule(const std::string& name) const
{
    auto it = m_ruleIndex.find(name);
    return it != m_ruleIndex.end() ? it->second : nullptr;
}

std::vector<FileNode*> BuildGraph::files() const
{
    std::vector<FileNode*> result;
    result.reserve(m_files.size());
    for (const auto& [_, node] : m_files)
        result.push_back(node.get());
    return result;
}

} // namespace maketex
