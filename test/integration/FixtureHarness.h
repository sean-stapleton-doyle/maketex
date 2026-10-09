#pragma once

// Helpers for end-to-end tests that run real TeX engines against the
// projects under testdata/. Each test builds a private copy of a fixture so
// runs never touch the source tree and can execute in parallel.

#include "maketex/Build.h"

#include <filesystem>
#include <string>
#include <vector>

namespace maketex::integration {

// Result of one runBuild call plus what the observer saw.
struct BuildRun {
    DriverResult result;
    std::vector<std::string> toolsRun; // in order, one entry per tool run
    bool upToDate = false; // the incremental check skipped the build
};

// True when `program` resolves to an executable on PATH.
bool hasProgram(const std::string& program);

// True when kpsewhich finds `file` (e.g. "biblatex.sty") in the TeX tree.
bool hasTexFile(const std::string& file);

// A private, source-only copy of testdata/<name> in a per-test temp dir.
class FixtureCopy {
public:
    explicit FixtureCopy(const std::string& name);
    ~FixtureCopy();

    FixtureCopy(const FixtureCopy&) = delete;
    FixtureCopy& operator=(const FixtureCopy&) = delete;

    const std::filesystem::path& dir() const { return m_dir; }
    std::filesystem::path path(const std::string& relative) const { return m_dir / relative; }

    // Options for building <dir>/main.tex with the library defaults.
    BuildOptions options(const std::string& texFile = "main.tex") const;

    BuildRun build(BuildOptions options) const;
    BuildRun build() const { return build(options()); }

    // Appends text to a file in the copy (creating it if needed).
    void append(const std::string& relative, const std::string& text) const;
    // Replaces the first occurrence of `from` with `to` in a file.
    void replace(const std::string& relative, const std::string& from,
        const std::string& to) const;
    void write(const std::string& relative, const std::string& content) const;

private:
    std::filesystem::path m_dir;
};

} // namespace maketex::integration
