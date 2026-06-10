#pragma once

#include "IFileHasher.h"

namespace maketex {

class Blake3Hasher : public IFileHasher {
public:
    std::expected<Hash, std::error_code> hashFile(
        const std::filesystem::path& path,
        const std::optional<std::regex>& ignorePattern = {}) const override;
};

} // namespace maketex
