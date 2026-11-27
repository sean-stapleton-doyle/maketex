#include "commands/CleanCommand.h"

#include "CleanPlan.h"

#include "maketex/StateDatabase.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    class SystemCleanEnv final : public ICleanEnv {
    public:
        std::optional<BuildFileSets> scanBuildFiles(const BuildOptions& build) override
        {
            return maketex::scanBuildFiles(build);
        }

        std::vector<fs::path> publishedArtifactPaths(const BuildOptions& build) override
        {
            return maketex::publishedArtifactPaths(build);
        }

        std::vector<std::string> readDbJobs(const fs::path& db) override
        {
            StateDatabase state(
                std::make_unique<SQLite::Database>(db.string(), SQLite::OPEN_READWRITE));
            return state.jobs();
        }

        void forgetDbJob(const fs::path& db, const std::string& jobName) override
        {
            StateDatabase state(
                std::make_unique<SQLite::Database>(db.string(), SQLite::OPEN_READWRITE));
            state.forgetJob(jobName);
        }

        std::uintmax_t removeAll(const fs::path& path, std::error_code& ec) override
        {
            return fs::remove_all(path, ec);
        }

        void removeIfEmptyDir(const fs::path& dir) override
        {
            std::error_code ec;
            if (fs::is_directory(dir, ec) && fs::is_empty(dir, ec))
                fs::remove(dir, ec);
        }

        std::ostream& out() override { return std::cout; }
        std::ostream& err() override { return std::cerr; }
    };

    CleanPlan buildEffectiveCleanPlan(const EffectiveOptions& options, ICleanEnv& env,
        const fs::path& outputDir, const fs::path& texDir, const std::string& jobName)
    {
        const BuildOptions& build = options.build;
        // `-o .`: the output directory is the user's source directory.
        std::error_code sameDirEc;
        const bool userDirectory = fs::equivalent(outputDir, texDir, sameDirEc);

        // In default-output mode the PDF/SyncTeX copies published beside the
        // tex file are this build's products too, and a pre-.maketex state
        // database may still sit next to the sources.
        std::vector<fs::path> published = env.publishedArtifactPaths(build);
        if (build.usesDefaultOutputDir())
            published.push_back(texDir / ".maketex.db"); // pre-.maketex layout

        auto plan = computeCleanPlan(env.scanBuildFiles(build), build.texFile,
            outputDir, jobName, options.extras.cleanAll,
            options.extras.cleanExtraExtensions, published, userDirectory);

        // Builds made before the hidden default directory existed left their
        // aux files next to the sources. When the default location has no build
        // record but the tex directory does, sweep the legacy layout as well.
        if (build.usesDefaultOutputDir()
            && !fs::exists(outputDir / (jobName + ".fls"))
            && fs::exists(texDir / (jobName + ".fls"))) {
            BuildOptions legacy = build;
            legacy.outputDir = texDir;
            mergePlans(plan,
                computeCleanPlan(env.scanBuildFiles(legacy), build.texFile, texDir,
                    jobName, options.extras.cleanAll,
                    options.extras.cleanExtraExtensions, { }, /*userDirectory=*/true));
        }

        return plan;
    }

    bool applySharedDatabasePolicy(CleanPlan& plan, const fs::path& db,
        const std::string& jobName, ICleanEnv& env)
    {
        if (std::ranges::find(plan.remove, db) == plan.remove.end())
            return false;
        try {
            return preserveSharedDatabase(plan, db, jobName, env.readDbJobs(db));
        } catch (const std::exception&) {
            // Unreadable database: removing it is the right cleanup.
            return false;
        }
    }

    int executeRemovals(const CleanPlan& plan, ICleanEnv& env, std::uintmax_t& removed)
    {
        int failures = 0;
        for (const auto& p : plan.remove) {
            std::error_code ec;
            // remove_all: the pythontex output directory is a candidate too.
            auto count = env.removeAll(p, ec);
            if (ec) {
                env.err() << "maketex: cannot remove " << p.string() << ": "
                          << ec.message() << "\n";
                ++failures;
            } else {
                removed += count;
                env.out() << "removed " << p.string() << "\n";
            }
        }
        return failures;
    }

} // namespace

int runCleanCommand(const EffectiveOptions& options, ICleanEnv& env)
{
    const BuildOptions& build = options.build;
    const fs::path outputDir = build.resolvedOutputDir();
    const fs::path texDir = build.texFile.parent_path();
    const std::string jobName = jobNameFor(build);

    auto plan = buildEffectiveCleanPlan(options, env, outputDir, texDir, jobName);

    // The state database is shared by every document built into this
    // output directory: --all forgets only this one, and deletes the file
    // only when no other document has state in it.
    const fs::path db = outputDir / ".maketex.db";
    const bool forgetInSharedDb = applySharedDatabasePolicy(plan, db, jobName, env);

    for (const auto& p : plan.skippedOutside)
        env.err() << "maketex: skipping " << p.string()
                  << " (outside the output directory)\n";

    if (forgetInSharedDb && !options.extras.dryRun) {
        env.forgetDbJob(db, jobName);
        env.out() << "forgot " << jobName << " in " << db.string()
                  << " (other documents still use it)\n";
    }

    if (plan.remove.empty()) {
        env.out() << "maketex: nothing to clean\n";
        return 0;
    }

    if (options.extras.dryRun) {
        for (const auto& p : plan.remove)
            env.out() << "would remove " << p.string() << "\n";
        env.out() << "maketex: " << plan.remove.size() << " file"
                  << (plan.remove.size() == 1 ? "" : "s")
                  << " (dry run, nothing deleted)\n";
        return 0;
    }

    std::uintmax_t removed = 0;
    int failures = executeRemovals(plan, env, removed);

    // The hidden default directory is ours; drop it once nothing is left
    // inside (only --all empties it — otherwise the PDF/db remain).
    if (build.usesDefaultOutputDir())
        env.removeIfEmptyDir(outputDir);

    env.out() << "maketex: removed " << removed << " file"
              << (removed == 1 ? "" : "s") << "\n";
    return failures == 0 ? 0 : 1;
}

int runCleanCommand(const EffectiveOptions& options)
{
    SystemCleanEnv env;
    return runCleanCommand(options, env);
}

} // namespace maketex::cli
