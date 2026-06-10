#include "graph/BuildGraph.h"

#include <queue>
#include <stdexcept>

namespace maketex {

namespace fs = std::filesystem;

FileNode& BuildGraph::getOrAddFile(fs::path path, FileRole role)
{
    auto key = fs::weakly_canonical(path).string();
    auto it = m_files.find(key);
    if (it != m_files.end())
        return *it->second;

    auto node = std::make_unique<FileNode>(fs::weakly_canonical(path), role);
    auto& ref = *node;
    m_files.emplace(key, std::move(node));
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

std::vector<RuleNode*> BuildGraph::rules() const
{
    std::vector<RuleNode*> result;
    result.reserve(m_rules.size());
    for (const auto& node : m_rules)
        result.push_back(node.get());
    return result;
}

std::vector<RuleNode*> BuildGraph::topologicalRuleOrder() const
{
    // Map each output file path to the rule that produces it.
    std::unordered_map<std::string, RuleNode*> producedBy;
    for (const auto& rule : m_rules) {
        for (const auto& out : rule->outputs())
            producedBy[out->path().string()] = rule.get();
    }

    // Build successor lists and in-degree counts.
    std::unordered_map<RuleNode*, std::vector<RuleNode*>> successors;
    std::unordered_map<RuleNode*, int> inDegree;
    for (const auto& rule : m_rules) {
        successors[rule.get()]; // ensure key exists
        inDegree[rule.get()] = 0;
    }

    for (const auto& rule : m_rules) {
        for (const auto& input : rule->inputs()) {
            auto it = producedBy.find(input->path().string());
            if (it == producedBy.end() || it->second == rule.get())
                continue; // external source file, or self-loop (e.g. .aux)
            successors[it->second].push_back(rule.get());
            inDegree[rule.get()]++;
        }
    }

    // Kahn's algorithm — seeds with all zero-in-degree rules, preserving
    // insertion order for determinism when multiple rules are ready.
    std::vector<RuleNode*> result;
    result.reserve(m_rules.size());

    std::queue<RuleNode*> ready;
    for (const auto& rule : m_rules) {
        if (inDegree[rule.get()] == 0)
            ready.push(rule.get());
    }

    while (!ready.empty()) {
        auto* rule = ready.front();
        ready.pop();
        result.push_back(rule);
        for (auto* succ : successors[rule]) {
            if (--inDegree[succ] == 0)
                ready.push(succ);
        }
    }

    if (result.size() != m_rules.size())
        throw std::runtime_error("Cycle detected in build rule graph");

    return result;
}

} // namespace maketex
