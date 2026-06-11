#pragma once

#include "maketex/graph/FileNode.h"
#include "maketex/graph/RuleNode.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace maketex {

// Bipartite directed graph of FileNodes and RuleNodes.
//
// Ownership: the graph owns all nodes via unique_ptr; returned pointers and
// references are stable for the graph's lifetime.
//
// Edges are stored on RuleNode (addInput / addOutput). The graph's role is
// ownership and lookup; staleness policy lives on RuleNode, scheduling on
// BuildDriver.
class BuildGraph {
public:
    // Returns the existing node if one with this path already exists (role ignored).
    // Paths are weakly-canonicalised before lookup to prevent duplicates from
    // relative vs. absolute representations of the same file.
    FileNode& getOrAddFile(std::filesystem::path path, FileRole role);

    // Returns the existing node if one with this name already exists.
    RuleNode& getOrAddRule(std::string name);

    // nullptr if not found.
    FileNode* findFile(const std::filesystem::path& path) const;
    RuleNode* findRule(const std::string& name) const;

    // All file nodes (unordered).
    std::vector<FileNode*> files() const;

private:
    // Keyed by weakly-canonical path string for deduplication.
    std::unordered_map<std::string, std::unique_ptr<FileNode>> m_files;

    // Insertion-ordered for deterministic iteration.
    std::vector<std::unique_ptr<RuleNode>> m_rules;
    std::unordered_map<std::string, RuleNode*> m_ruleIndex;
};

} // namespace maketex
