#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace maketex {

// The parts of a biblatex control file (.bcf) maketex schedules on.
struct BcfRecord {
    // Database names from <bcf:datasource type="file" …>NAME</bcf:datasource>,
    // as given to \addbibresource — the caller locates the files.
    std::vector<std::string> dataSources;
};

class BcfParser {
public:
    std::expected<BcfRecord, std::error_code> parse(
        const std::filesystem::path& path) const;
};

} // namespace maketex
