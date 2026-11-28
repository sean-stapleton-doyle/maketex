#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace maketex::diagnostics {

// Filesystem queries made while attributing log output to source files.
class ISourceResolver {
public:
    virtual ~ISourceResolver() = default;
    virtual bool isFile(const std::filesystem::path& p) const = 0;
    virtual std::filesystem::path canonical(const std::filesystem::path& p) const = 0;
    virtual std::optional<std::string> readText(const std::filesystem::path& p) const = 0;
};

const ISourceResolver& filesystemResolver();

} // namespace maketex::diagnostics
