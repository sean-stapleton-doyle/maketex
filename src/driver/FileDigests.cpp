#include "maketex/driver/FileDigests.h"

namespace maketex::driver {

namespace fs = std::filesystem;

FileDigests::FileDigests(BuildGraph& graph, FileStateTracker& tracker, IFileHasher& hasher)
    : m_graph(graph)
    , m_tracker(tracker)
    , m_hasher(hasher)
{
}

std::string FileDigests::canonicalKey(const fs::path& path)
{
    return fs::weakly_canonical(path).string();
}

std::expected<std::optional<Hash>, std::error_code> FileDigests::currentHash(
    const fs::path& path)
{
    auto& node = m_graph.getOrAddFile(path, FileRole::Source);
    auto state = node.refresh(m_tracker);
    if (!state)
        return std::unexpected(state.error());
    if (!state->exists)
        return std::optional<Hash> { };
    return std::optional<Hash> { state->hash };
}

std::expected<Digests, std::error_code> FileDigests::digestInputs(
    std::span<const ToolInput> inputs)
{
    Digests digests;
    for (const auto& input : inputs) {
        if (input.content) {
            digests[canonicalKey(input.path)] = m_hasher.hashBytes(*input.content);
            continue;
        }
        auto hash = currentHash(input.path);
        if (!hash)
            return std::unexpected(hash.error());
        if (*hash)
            digests[canonicalKey(input.path)] = **hash;
    }
    return digests;
}

std::expected<Digests, std::error_code> FileDigests::takeSnapshot()
{
    Digests snap;
    for (auto* f : m_graph.files()) {
        if (f->role() == FileRole::Source || isFinalProduct(f->path()))
            continue;
        auto state = f->refresh(m_tracker);
        if (!state)
            return std::unexpected(state.error());
        if (state->exists)
            snap[f->path().string()] = state->hash;
    }
    return snap;
}

} // namespace maketex::driver
