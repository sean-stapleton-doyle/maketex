// End-to-end harness for libmaketex. The real CLI will live elsewhere and
// add config-file handling; this binary exists to exercise the library's
// public surface (Build.h) against real documents.

#include "maketex/Build.h"
#include "maketex/DiagnosticFormatter.h"
#include "maketex/engines/EngineFactory.h"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

static constexpr std::string_view kUsage = "Usage: maketex [-v|--verbose] [--engine=pdflatex|xelatex|lualatex] <file.tex>\n";

static int run(int argc, char* argv[])
{
    bool verbose = false;
    fs::path texFile;
    std::string_view engineName = "pdflatex";

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg.starts_with("--engine=")) {
            engineName = arg.substr(std::string_view("--engine=").size());
        } else if (arg == "--help" || arg == "-h") {
            std::cout << kUsage;
            return 0;
        } else if (arg.starts_with("-")) {
            std::cerr << "maketex: unknown option: " << arg << "\n"
                      << kUsage;
            return 1;
        } else if (texFile.empty()) {
            texFile = fs::absolute(arg);
        } else {
            std::cerr << "maketex: multiple input files given\n"
                      << kUsage;
            return 1;
        }
    }

    if (texFile.empty()) {
        std::cerr << kUsage;
        return 1;
    }
    if (!fs::exists(texFile)) {
        std::cerr << "maketex: file not found: " << texFile << "\n";
        return 1;
    }

    auto engineKind = maketex::parseEngineKind(engineName);
    if (!engineKind) {
        std::cerr << "maketex: unknown engine: " << engineName << "\n"
                  << kUsage;
        return 1;
    }

    std::cout << "maketex: building " << texFile.filename().string() << "\n";

    auto result = maketex::runBuild({
        .texFile = texFile,
        .engine = *engineKind,
    });

    if (result.success) {
        std::cout << "maketex: done in " << result.passes
                  << " pass" << (result.passes == 1 ? "" : "es") << "\n";
        maketex::diagnostics::format(result.diagnostics, std::cerr, verbose);
        return 0;
    }

    std::cerr << "maketex: failed after " << result.passes
              << " pass" << (result.passes == 1 ? "" : "es")
              << ": " << result.lastError << "\n";
    maketex::diagnostics::format(result.diagnostics, std::cerr, verbose);

    return 1;
}

int main(int argc, char* argv[])
{
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        // Spawn failures (latex not installed), database errors, etc.
        std::cerr << "maketex: " << e.what() << "\n";
        return 1;
    }
}
