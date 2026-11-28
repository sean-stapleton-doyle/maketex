#include "maketex/build/Environment.h"
#include "maketex/build/Fingerprint.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

using maketex::BuildOptions;
using maketex::HashTriggeredTool;
using maketex::ToolWorkingDir;
using maketex::build::buildFingerprint;
using maketex::build::EnvLookup;
using maketex::build::prependTexFormats;
using maketex::build::searchPathEnv;

namespace {

EnvLookup mapEnv(std::map<std::string, std::string> vars)
{
    return [vars = std::move(vars)](std::string_view key) -> std::optional<std::string> {
        if (auto it = vars.find(std::string(key)); it != vars.end())
            return it->second;
        return std::nullopt;
    };
}

} // namespace

TEST(FingerprintTest, isDeterministicAndSensitiveToEveryField)
{
    const EnvLookup emptyEnv = mapEnv({ });
    const std::vector<std::string_view> baseTools { "biber", "bibtex" };
    BuildOptions base { .texFile = "/project/main.tex" };

    const std::string fpBase = buildFingerprint(base, "pdflatex", baseTools, emptyEnv);
    EXPECT_EQ(fpBase, buildFingerprint(base, "pdflatex", baseTools, emptyEnv));

    // Engine name and tool list
    EXPECT_NE(fpBase, buildFingerprint(base, "lualatex", baseTools, emptyEnv));
    const std::vector<std::string_view> reorderedTools { "bibtex", "biber" };
    EXPECT_NE(fpBase, buildFingerprint(base, "pdflatex", reorderedTools, emptyEnv));

    // Engine program & extra options
    {
        auto o = base;
        o.engineArguments.program = "/opt/tex/pdflatex";
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.engineArguments.extraOptions = { "-draftmode" };
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }

    // Boolean flags
    {
        auto o = base;
        o.shellEscape = !o.shellEscape;
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.synctex = !o.synctex;
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.haltOnError = !o.haltOnError;
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.preambleCache = !o.preambleCache;
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }

    // Diagnostic policy
    {
        auto o = base;
        o.diagnosticPolicy.warningsAsErrors = true;
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.diagnosticPolicy.failOn = { "undefined-reference" };
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.diagnosticPolicy.ignore = { "overfull-box" };
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }

    // Content filters & extraEnv
    {
        auto o = base;
        o.contentFilters.emplace_back("aux", "^%temp");
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.extraEnv.emplace_back("SOURCE_DATE_EPOCH", "1700000000");
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }

    // Search paths
    {
        auto o = base;
        o.searchPaths.texInputs = { "styles//" };
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.searchPaths.bibInputs = { "bib/" };
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.searchPaths.bstInputs = { "bst/" };
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
    {
        auto o = base;
        o.searchPaths.indexStyle = { "ist/" };
        EXPECT_NE(fpBase, buildFingerprint(o, "pdflatex", baseTools, emptyEnv));
    }
}

TEST(FingerprintTest, reflectsCustomToolDetails)
{
    const EnvLookup emptyEnv = mapEnv({ });
    BuildOptions o1 { .texFile = "/project/main.tex" };
    HashTriggeredTool::Recipe recipe {
        .name = "svg2pdf",
        .argv = { "inkscape", "{job}.svg" },
        .triggerExtensions = { ".svg" },
        .outputExtensions = { ".pdf" },
        .workingDir = ToolWorkingDir::Output,
    };
    o1.customTools.push_back(recipe);

    const std::string fp1 = buildFingerprint(o1, "pdflatex", { }, emptyEnv);
    EXPECT_NE(fp1.find(";custom=svg2pdf:inkscape {job}.svg .svg,.pdf,O"), std::string::npos);

    auto o2 = o1;
    o2.customTools[0].workingDir = ToolWorkingDir::Source;
    const std::string fp2 = buildFingerprint(o2, "pdflatex", { }, emptyEnv);
    EXPECT_NE(fp1, fp2);
    EXPECT_NE(fp2.find(",S;"), std::string::npos);
}

TEST(FingerprintTest, tracksInheritedKpathseaEnvVarsOnly)
{
    BuildOptions opts { .texFile = "/project/main.tex" };
    const std::string base = buildFingerprint(opts, "pdflatex", { }, mapEnv({ }));
    const std::string unrelated = buildFingerprint(
        opts, "pdflatex", { }, mapEnv({ { "UNRELATED_VAR", "xyz" } }));
    EXPECT_EQ(base, unrelated);

    for (const char* var : { "TEXINPUTS", "BIBINPUTS", "BSTINPUTS", "INDEXSTYLE" }) {
        const std::string withVar = buildFingerprint(
            opts, "pdflatex", { }, mapEnv({ { var, "/custom/path:" } }));
        EXPECT_NE(base, withVar) << "variable " << var << " should alter fingerprint";
    }
}

TEST(EnvironmentTest, searchPathEnvJoinsConfiguredMergedAndInheritedValues)
{
    BuildOptions opts {
        .texFile = "/project/main.tex",
        .searchPaths = {
            .texInputs = { "styles//", "common" },
            .bibInputs = { "refs" },
            .bstInputs = { "bst" },
            .indexStyle = { "ist" },
        },
        .extraEnv = {
            { "FOO", "bar" },
            { "TEXINPUTS", "from-extra:" },
        },
    };
    auto env = mapEnv({
        { "TEXINPUTS", "ignored-because-in-extra" },
        { "BIBINPUTS", "/inherited/bib:" },
        { "BSTINPUTS", "" },
    });

    auto resolved = searchPathEnv(opts, env);

    auto findVar = [&](std::string_view name) -> std::optional<std::string> {
        for (const auto& [k, v] : resolved)
            if (k == name)
                return v;
        return std::nullopt;
    };

    EXPECT_EQ(findVar("FOO"), "bar");
    EXPECT_EQ(findVar("TEXINPUTS"), "styles//:common:from-extra:");
    EXPECT_EQ(findVar("BIBINPUTS"), "refs:/inherited/bib:");
    EXPECT_EQ(findVar("BSTINPUTS"), "bst:");
    EXPECT_EQ(findVar("INDEXSTYLE"), "ist:");
}

TEST(EnvironmentTest, prependTexFormatsHandlesSetEmptyAndUnset)
{
    EXPECT_EQ(prependTexFormats("/project/.maketex", mapEnv({ { "TEXFORMATS", "/sys/fmt:" } })),
        "/project/.maketex:/sys/fmt:");
    EXPECT_EQ(prependTexFormats("/project/.maketex", mapEnv({ { "TEXFORMATS", "" } })),
        "/project/.maketex:");
    EXPECT_EQ(prependTexFormats("/project/.maketex", mapEnv({ })),
        "/project/.maketex:");
}
