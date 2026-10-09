#include "CliArgs.h"
#include "Config.h"
#include "EffectiveOptions.h"
#include "commands/BuildCommand.h"
#include "commands/CleanCommand.h"
#include "commands/DoctorCommand.h"
#include "commands/WatchCommand.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace maketex::cli;

static int run(int argc, char* argv[])
{
    auto args = normalizeArgv(std::vector<std::string>(argv + 1, argv + argc));

    auto parsed = parseCliArgs(args);
    if (!parsed.args)
        return parsed.exitCode;

    std::optional<Config> config;
    if (!parsed.args->noConfig) {
        std::optional<fs::path> configPath = parsed.args->configPath;
        if (!configPath) {
            fs::path searchStart = parsed.args->texFile
                ? fs::absolute(*parsed.args->texFile).parent_path()
                : fs::current_path();
            configPath = findConfigFile(searchStart);
        }
        if (configPath) {
            auto loaded = loadConfig(*configPath);
            if (!loaded) {
                std::cerr << "maketex: " << loaded.error() << "\n";
                return kExitUsage;
            }
            for (const auto& warning : loaded->warnings)
                std::cerr << "maketex: warning: " << warning << "\n";
            config = std::move(*loaded);
        }
    }

    auto options = assembleOptions(*parsed.args, config);
    if (!options) {
        std::cerr << "maketex: " << options.error() << "\n";
        return kExitUsage;
    }

    switch (options->command) {
    case Command::Build:
        return runBuildCommand(*options);
    case Command::Watch:
        return runWatchCommand(*options);
    case Command::Clean:
        return runCleanCommand(*options);
    case Command::Doctor:
        return runDoctorCommand(*options);
    }
    return 1;
}

int main(int argc, char* argv[])
{
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        // Spawn failures (latex not installed), database errors, etc.
        std::cerr << "maketex: " << e.what() << "\n";
        return kExitEnvironment;
    }
}
