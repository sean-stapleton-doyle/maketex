#include "graph/RuleNode.h"

#include <ctime>

namespace maketex {

RuleNode::RuleNode(std::string name)
    : m_name(std::move(name))
{
}

void RuleNode::addInput(FileNode* node)
{
    if (m_inputSet.insert(node).second)
        m_inputs.push_back(node);
}

void RuleNode::addOutput(FileNode* node)
{
    if (m_outputSet.insert(node).second)
        m_outputs.push_back(node);
}

bool RuleNode::isStale(const StateDatabase& db) const
{
    auto record = db.getRule(m_name);
    if (!record || !record->succeeded)
        return true;

    for (const auto& node : m_inputs) {
        auto stored = db.getRuleInputHash(m_name, node->path().string());
        if (!stored)
            return true; // new input, never recorded
        if (!node->state().exists)
            return true; // input disappeared
        if (*stored != node->state().hash)
            return true;
    }
    return false;
}

void RuleNode::markRan(StateDatabase& db, bool succeeded, const std::string& command) const
{
    // rules must exist before rule_inputs can reference it (FK constraint).
    db.upsertRule(RuleRecord {
        .name = m_name,
        .command = command,
        .ran_at = static_cast<int64_t>(std::time(nullptr)),
        .succeeded = succeeded,
    });

    std::vector<std::pair<std::string, Hash>> snapshots;
    snapshots.reserve(m_inputs.size());
    for (const auto& node : m_inputs) {
        if (node->state().exists)
            snapshots.emplace_back(node->path().string(), node->state().hash);
    }
    db.setRuleInputs(m_name, snapshots);
}

} // namespace maketex
