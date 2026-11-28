#pragma once

#include "maketex/FileStateTracker.h"
#include "maketex/IFileHasher.h"
#include "maketex/ITool.h"
#include "maketex/driver/PassAnalysis.h"
#include "maketex/graph/BuildGraph.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <system_error>

namespace maketex::driver {

// Graph-backed file digest queries shared by BuildDriver and ToolPass.
// Non-owning: graph, tracker, and hasher must outlive this helper.
class FileDigests {
public:
    FileDigests(BuildGraph& graph, FileStateTracker& tracker, IFileHasher& hasher);

    // Weakly-canonical path string used as a stable map key.
    static std::string canonicalKey(const std::filesystem::path& path);

    // Current hash of path via graph + tracker, or nullopt when absent.
    std::expected<std::optional<Hash>, std::error_code> currentHash(
        const std::filesystem::path& path);

    // Digest of each tool input: file contents via the tracker, derived inputs
    // via the hasher. Missing files are omitted.
    std::expected<Digests, std::error_code> digestInputs(std::span<const ToolInput> inputs);

    // Hashes every engine-written file except final products (PDF, log, ...) —
    // the files a following pass may read back.
    std::expected<Digests, std::error_code> takeSnapshot();

private:
    BuildGraph& m_graph;
    FileStateTracker& m_tracker;
    IFileHasher& m_hasher;
};

} // namespace maketex::driver
