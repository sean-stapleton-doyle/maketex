#pragma once

#include "StateDatabase.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <regex>
#include <system_error>

namespace maketex {

class IFileHasher {
public:
    virtual ~IFileHasher() = default;

    // Returns a BLAKE3 hash of the file at path.
    // When ignorePattern is set, lines matching it are excluded from the hash
    // input — used to suppress volatile fields such as EPS %%CreationDate.
    // Returns an error_code (e.g. no_such_file_or_directory) if the file
    // cannot be opened.
    virtual std::expected<Hash, std::error_code> hashFile(
        const std::filesystem::path& path,
        const std::optional<std::regex>& ignorePattern = {}) const = 0;
};

} // namespace maketex
