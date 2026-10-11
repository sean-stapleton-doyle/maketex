#include "maketex/graph/RuleNode.h"

#include <cassert>
#include <ctime>

namespace maketex {

RuleNode::RuleNode(std::string name)
    : m_name(std::move(name))
{
}

void RuleNode::addInput(FileNode* node)
{
    assert(node != nullptr);
    if (m_inputSet.insert(node).second)
        m_inputs.push_back(node);
}

void RuleNode::addOutput(FileNode* node)
{
    assert(node != nullptr);
    if (m_outputSet.insert(node).second)
        m_outputs.push_back(node);
}

void RuleNode::clearEdges()
{
    m_inputs.clear();
    m_outputs.clear();
    m_inputSet.clear();
    m_outputSet.clear();
}

bool RuleNode::isStale(const StateDatabase& db) const
{
    auto record = db.getRule(m_name);
    if (!record || !record->succeeded)
        return true;

    for (const auto& node : m_inputs) {
        auto stored = db.getRuleInputHash(m_name, node->path().string());
        if (!stored) {
            // Absent when the run was recorded (markRan keeps only existing
            // inputs) and still absent: a scratch file the run read and then
            // deleted, like minted's main.w18 or main.pyg. Nothing changed.
            if (!node->state().exists)
                continue;
            return true; // new input, never recorded
        }
        if (!node->state().exists)
            return true; // input disappeared
        if (*stored != node->state().hash)
            return true;
    }
    return false;
}

void RuleNode::markRan(StateDatabase& db, bool succeeded, const std::string& command)
{
    std::vector<std::pair<std::string, Hash>> snapshots;
    snapshots.reserve(m_inputs.size());
    for (const auto& node : m_inputs) {
        if (node->state().exists)
            snapshots.emplace_back(node->path().string(), node->state().hash);
    }
    db.recordRun(RuleRecord {
                     .name = m_name,
                     .command = command,
                     .ran_at = static_cast<int64_t>(std::time(nullptr)),
                     .succeeded = succeeded,
                 },
        snapshots);
}

} // namespace maketex
