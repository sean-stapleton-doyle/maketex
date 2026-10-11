#pragma once

#include "CliArgs.h"
#include "Config.h"

#include "maketex/Build.h"

#include <expected>
#include <optional>
#include <string>

namespace maketex::cli {

// How a finished build is reported on stdout.
enum class OutputFormat {
    Human, // styled InlineReport, or plain text when piped/--plain/NO_COLOR
    Json, // one JSON document; nothing else on stdout
    Github, // GitHub Actions ::error/::warning annotations
};

// CLI-layer settings that don't map onto BuildOptions.
struct CliExtras {
    bool verbose = false;
    bool plain = false;
    OutputFormat format = OutputFormat::Human;
    int watchIntervalMs = 300;
    std::vector<std::filesystem::path> watchExtraPaths;
    std::vector<std::string> cleanExtraExtensions;
    bool cleanAll = false;
    bool dryRun = false;
    bool noTui = false;
    // Run tlmgr for missing packages and rebuild. On by default.
    bool installMissing = true;
    // Set by --install-missing or the config key, as opposed to the
    // default — decides whether sudo installs are confirmed first, and
    // whether doctor installs at all.
    bool installMissingRequested = false;
};

struct EffectiveOptions {
    Command command = Command::Build;
    BuildOptions build;
    CliExtras extras;
};

// Folds flags over config over library defaults and validates the result:
// the tex file must resolve and exist, engine/tool names must parse, and the
// output directory is created for build/watch. Errors are user-facing.
std::expected<EffectiveOptions, std::string> assembleOptions(
    const CliArgs& args, const std::optional<Config>& config);

} // namespace maketex::cli
