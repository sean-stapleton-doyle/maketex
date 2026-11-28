#include "maketex/build/Publish.h"

#include "maketex/Build.h"
#include "maketex/build/TexSystemRoots.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::build {

std::string publishedStamp(const fs::path& outDir, const fs::path& destDir,
    std::string_view jobName)
{
    std::string stamp;
    for (const char* ext : { ".pdf", ".synctex.gz" }) {
        fs::path src = outDir / (std::string(jobName) + ext);
        fs::path dest = destDir / (std::string(jobName) + ext);
        std::error_code ec;
        if (!fs::is_regular_file(src, ec))
            continue;
        auto sTok = fileStampToken(src);
        auto dTok = fileStampToken(dest);
        if (sTok.empty() || dTok.empty())
            return { }; // missing published file
        stamp += ext;
        stamp += '=';
        stamp += sTok;
        stamp += '/';
        stamp += dTok;
        stamp += ';';
    }
    return stamp;
}

bool canReusePublished(bool upToDate,
    const std::optional<std::string>& recordedStamp,
    std::string_view currentStamp)
{
    return upToDate && recordedStamp && !currentStamp.empty()
        && *recordedStamp == currentStamp;
}

std::vector<fs::path> publishResultArtifacts(bool upToDate,
    const fs::path& outDir,
    const fs::path& destDir,
    const std::string& resolvedJob,
    StateDatabase& stateDb)
{
    auto recorded = stateDb.getJobMeta(resolvedJob, "published_stamp");
    auto nowStamp = upToDate ? publishedStamp(outDir, destDir, resolvedJob) : std::string { };
    if (canReusePublished(upToDate, recorded, nowStamp)) {
        std::vector<fs::path> reused;
        for (const char* ext : { ".pdf", ".synctex.gz" }) {
            fs::path dest = destDir / (resolvedJob + ext);
            std::error_code ec;
            if (fs::is_regular_file(dest, ec))
                reused.push_back(std::move(dest));
        }
        return reused;
    }
    auto published = publishArtifacts(outDir, destDir, resolvedJob);
    if (auto fresh = publishedStamp(outDir, destDir, resolvedJob); !fresh.empty())
        stateDb.setJobMeta(resolvedJob, "published_stamp", fresh);
    return published;
}

} // namespace maketex::build

namespace maketex {

namespace {

    bool sameContent(const fs::path& a, const fs::path& b)
    {
        std::error_code ecA;
        std::error_code ecB;
        auto sizeA = fs::file_size(a, ecA);
        auto sizeB = fs::file_size(b, ecB);
        if (ecA || ecB || sizeA != sizeB)
            return false;
        std::ifstream fa(a, std::ios::binary);
        std::ifstream fb(b, std::ios::binary);
        std::array<char, 65536> ba;
        std::array<char, 65536> bb;
        while (fa && fb) {
            fa.read(ba.data(), ba.size());
            fb.read(bb.data(), bb.size());
            if (fa.gcount() != fb.gcount()
                || !std::equal(ba.begin(), ba.begin() + fa.gcount(), bb.begin()))
                return false;
        }
        return fa.eof() && fb.eof();
    }

} // namespace

std::vector<fs::path> publishArtifacts(const fs::path& outDir,
    const fs::path& destDir, const std::string& jobName)
{
    std::vector<fs::path> published;
    std::error_code ec;
    if (fs::equivalent(outDir, destDir, ec))
        return published;

    for (const char* ext : { ".pdf", ".synctex.gz" }) {
        fs::path src = outDir / (jobName + ext);
        if (!fs::is_regular_file(src, ec))
            continue;
        fs::path dest = destDir / (jobName + ext);
        // Already published from this build: copying again would only bump
        // the mtime and make PDF viewers reload for nothing.
        if (sameContent(src, dest)) {
            published.push_back(std::move(dest));
            continue;
        }
        // Copy to a sibling temp file first: rename within destDir is atomic
        // on POSIX, so a viewer refreshing mid-publish sees old or new,
        // never a torn file. The temp name carries the pid so two processes
        // publishing at once never write the same temp file.
        fs::path tmp = dest;
        tmp += ".maketex-tmp-" + std::to_string(::getpid());
        // copy_file returns bool; rename (ec overload) returns void, so check
        // ec explicitly after the call.
        bool copied = fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec);
        if (!ec)
            fs::rename(tmp, dest, ec);
        if (ec || !copied) {
            // Publish failures are non-fatal — the build already succeeded;
            // remove a half-written temp so the next attempt is clean. The
            // missing artifact is reflected by its absence from the returned
            // vector, which the caller treats as best-effort.
            std::error_code removeEc;
            fs::remove(tmp, removeEc);
            continue;
        }
        published.push_back(std::move(dest));
    }
    return published;
}

} // namespace maketex
