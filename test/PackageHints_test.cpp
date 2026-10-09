#include "PackageHints.h"

#include <gtest/gtest.h>

using maketex::Diagnostic;
using maketex::cli::installHint;
using maketex::cli::PackageLookup;
using maketex::cli::packageProviding;
using maketex::cli::TexDistribution;

namespace {

// A lookup that must not be consulted.
const PackageLookup kNoLookup = [](const std::string& file) -> std::optional<std::string> {
    ADD_FAILURE() << "unexpected lookup of " << file;
    return std::nullopt;
};

} // namespace

TEST(PackageHints, knownFilesNeedNoLookup)
{
    EXPECT_EQ(packageProviding("tikz.sty", kNoLookup), "pgf");
    EXPECT_EQ(packageProviding("makeglossaries", kNoLookup), "glossaries");
}

TEST(PackageHints, unknownFilesAreLookedUp)
{
    PackageLookup lookup = [](const std::string& file) -> std::optional<std::string> {
        return file == "fancyhdr.sty" ? std::optional<std::string>("fancyhdr") : std::nullopt;
    };
    EXPECT_EQ(packageProviding("fancyhdr.sty", lookup), "fancyhdr");
}

TEST(PackageHints, texLiveSuggestsTlmgr)
{
    EXPECT_EQ(installHint("minted.sty", TexDistribution::TexLive, kNoLookup),
        "install it: sudo tlmgr install minted");
    EXPECT_EQ(installHint("biber", TexDistribution::TexLive, kNoLookup),
        "install it: sudo tlmgr install biber");
}

TEST(PackageHints, texLiveFallsBackToTlmgrSearch)
{
    PackageLookup none = [](const std::string&) { return std::optional<std::string> { }; };
    EXPECT_EQ(installHint("obscure.sty", TexDistribution::TexLive, none),
        "find the package that provides it: tlmgr search --global --file /obscure.sty");
}

TEST(PackageHints, distributionPackagersGetTheirOwnCommand)
{
    EXPECT_EQ(installHint("minted.sty", TexDistribution::Fedora, kNoLookup),
        "install it: sudo dnf install 'tex(minted.sty)'");
    EXPECT_NE(installHint("minted.sty", TexDistribution::Debian, kNoLookup).find("apt"),
        std::string::npos);
    EXPECT_EQ(installHint("minted.sty", TexDistribution::MiKTeX, kNoLookup),
        "install it: miktex packages install minted");
}

TEST(PackageHints, pygmentsIsNotATexPackage)
{
    EXPECT_NE(installHint("pygmentize", TexDistribution::TexLive, kNoLookup).find("pip install pygments"),
        std::string::npos);
}

TEST(PackageHints, userInputFilesGetNoHint)
{
    // A missing \input{chapter} is the user's own file.
    std::vector<Diagnostic> diags { Diagnostic {
        .message = "File `chapter.tex' not found",
        .category = "missing-file",
        .missingFile = "chapter.tex",
    } };
    maketex::cli::addInstallHints(diags);
    EXPECT_TRUE(diags[0].hint.empty());
}
