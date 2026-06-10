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

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: maketex <file.tex>\n";
        return 1;
    }

    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--verbose" || std::string_view(argv[i]) == "-v")
            verbose = true;
    }

    fs::path texFile = fs::absolute(argv[1]);
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
        .auxDir = outDir,
        .recorder = true,
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
