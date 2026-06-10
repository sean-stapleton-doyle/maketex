#include "Blake3Hasher.h"
#include "BuildDriver.h"
#include "DiagnosticFormatter.h"
#include "EngineInvocation.h"
#include "ITool.h"
#include "InvokerKind.h"
#include "Process.h"
#include "StateDatabase.h"
#include "ToolFactory.h"
#include "engines/PdfLatexEngine.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

// TeX distribution roots (TEXMFROOT, system/user caches), resolved via
// kpsewhich. Files under these are excluded from dependency tracking — the
// engine reads hundreds of .sty/.cls/font files per pass, and treating them
// as inputs would mean hashing the whole distribution closure every build.
// Returns an empty list (track everything) when kpsewhich is unavailable.
static std::vector<fs::path> texSystemRoots()
{
    std::vector<fs::path> roots;
    for (const char* var : { "TEXMFROOT", "TEXMFSYSVAR", "TEXMFVAR" }) {
        try {
            std::vector<std::string> args { "kpsewhich", "-var-value", var };
            auto result = maketex::Process(args).wait();
            if (result.exitCode != 0)
                continue;
            auto value = result.stdoutOutput;
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
                value.pop_back();
            if (!value.empty())
                roots.emplace_back(value);
        } catch (const std::system_error&) {
            break; // kpsewhich not installed — nothing to ignore
        }
    }
    return roots;
}

static constexpr std::string_view kUsage = "Usage: maketex [-v|--verbose] <file.tex>\n";

static int run(int argc, char* argv[])
{
    bool verbose = false;
    fs::path texFile;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << kUsage;
            return 0;
        } else if (arg.starts_with("-")) {
            std::cerr << "maketex: unknown option: " << arg << "\n" << kUsage;
            return 1;
        } else if (texFile.empty()) {
            texFile = fs::absolute(arg);
        } else {
            std::cerr << "maketex: multiple input files given\n" << kUsage;
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

    fs::path outDir = texFile.parent_path();
    fs::path dbPath = outDir / ".maketex.db";

    auto db = std::make_unique<SQLite::Database>(
        dbPath.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    maketex::StateDatabase stateDb(std::move(db));

    maketex::Blake3Hasher hasher;
    maketex::PdfLatexEngine engine;

    maketex::ToolFactory factory(hasher);
    std::vector<std::unique_ptr<maketex::ITool>> tools;
    tools.push_back(factory.hashTriggered(
        "biber", ".bcf", { "biber", "$job" }, maketex::InvokerKind::Biber, /*runOnce=*/true));
    tools.push_back(factory.hashTriggered(
        "makeindex", ".idx", { "makeindex", "$job" }, maketex::InvokerKind::Makeindex));
    tools.push_back(factory.hashTriggered(
        "makeglossaries", ".glo", { "makeglossaries", "$job" }, maketex::InvokerKind::Makeglossaries));
    tools.push_back(factory.hashTriggered(
        "pythontex", ".pytxcode", { "pythontex", "$job" }, maketex::InvokerKind::Pythontex));
    tools.push_back(factory.bibtex());

    std::vector<maketex::ITool*> toolPtrs;
    toolPtrs.reserve(tools.size());
    for (auto& t : tools)
        toolPtrs.push_back(t.get());

    maketex::EngineInvocation inv {
        .texFile = texFile,
        .outputDir = outDir,
        .haltOnError = true,
    };

    std::cout << "maketex: building " << texFile.filename().string() << "\n";

    maketex::BuildDriver driver(engine, stateDb, hasher, inv,
        maketex::BuildDriver::kDefaultMaxPasses, std::move(toolPtrs),
        texSystemRoots());
    auto result = driver.run();

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
