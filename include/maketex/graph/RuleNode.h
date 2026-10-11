#pragma once

#include "maketex/StateDatabase.h"
#include "maketex/graph/FileNode.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace maketex {

// A node in the build dependency graph representing a single tool invocation.
//
// RuleNode holds non-owning FileNode* pointers — the BuildGraph owns all nodes.
// addInput / addOutput are idempotent: adding the same node twice has no effect.
//
// Callers must refresh() all input FileNodes before calling isStale().
class RuleNode {
public:
    explicit RuleNode(std::string name);

    const std::string& name() const { return m_name; }

    void addInput(FileNode* node);
    void addOutput(FileNode* node);

    // Forgets every input and output edge — before re-wiring the rule from
    // a fresh record of what it read, so a dropped dependency goes away.
    void clearEdges();

    const std::vector<FileNode*>& inputs() const { return m_inputs; }
    const std::vector<FileNode*>& outputs() const { return m_outputs; }

    // Returns true if the rule should run:
    //   - no prior successful run recorded in db, OR
    //   - any input's current hash (node.state()) differs from the stored hash, OR
    //   - an input has no stored hash (newly added dependency) and exists —
    //     one that was absent at the recorded run and still is (a scratch
    //     file the run read and deleted) is not a change.
    bool isStale(const StateDatabase& db) const;

    // Persists the current input hashes and updates the rule record.
    // Call after every execution, whether or not it succeeded.
    // Input nodes must have been refresh()ed before this call.
    // Not const: it writes the rule record and input hashes to the database.
    void markRan(StateDatabase& db, bool succeeded, const std::string& command);

private:
    std::string m_name;
    std::vector<FileNode*> m_inputs;
    std::vector<FileNode*> m_outputs;
    std::unordered_set<FileNode*> m_inputSet;
    std::unordered_set<FileNode*> m_outputSet;
};

} // namespace maketex
