#include "maketex/diagnostics/FileStack.h"

#include "FakeSourceResolver.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace maketex::diagnostics;
using maketex::test_utils::FakeSourceResolver;
namespace fs = std::filesystem;

TEST(DiagnosticsFileStack, hasTexSourceExtensionTable)
{
    for (const char* ext : { "a.tex", "a.sty", "a.cls", "a.def", "a.ltx", "a.cfg",
             "a.clo", "a.fd", "a.bbl", "a.toc", "a.ind", "a.bib" })
        EXPECT_TRUE(hasTexSourceExtension(ext)) << ext;

    for (const char* nonTex : { "a.aux", "a.pdf", "a.log", "a.txt", "noext", "" })
        EXPECT_FALSE(hasTexSourceExtension(nonTex)) << nonTex;
}

TEST(DiagnosticsFileStack, displayForAndTokenResolution)
{
    FakeSourceResolver resolver;
    resolver.addFile("/project/main.tex");
    resolver.addFile("/project/chapters/ch1.tex");
    resolver.addFile("/usr/local/texlive/article.cls");
    resolver.addCanonicalAlias("/project/link/ch1.tex", "/project/chapters/ch1.tex");

    const fs::path workDir = "/project";

    // Relative path is lexically normalised.
    EXPECT_EQ(displayFor("./chapters/ch1.tex", workDir, resolver), "chapters/ch1.tex");
    // Absolute path under workingDir becomes relative.
    EXPECT_EQ(displayFor("/project/chapters/ch1.tex", workDir, resolver), "chapters/ch1.tex");
    // Symlink/alias under workingDir canonicalises cleanly.
    EXPECT_EQ(displayFor("/project/link/ch1.tex", workDir, resolver), "chapters/ch1.tex");
    // Absolute path outside workingDir stays absolute (not "../...").
    EXPECT_EQ(displayFor("/usr/local/texlive/article.cls", workDir, resolver),
        "/usr/local/texlive/article.cls");
    // Empty workingDir keeps normalised absolute path.
    EXPECT_EQ(displayFor("/project/main.tex", { }, resolver), "/project/main.tex");

    EXPECT_FALSE(resolveToken("", workDir, resolver).has_value());
    EXPECT_FALSE(resolveToken("./missing.tex", workDir, resolver).has_value());

    auto rel = resolveToken("./chapters/ch1.tex", workDir, resolver);
    ASSERT_TRUE(rel.has_value());
    EXPECT_EQ(rel->display, "chapters/ch1.tex");
    EXPECT_EQ(rel->abs, fs::path("/project/chapters/ch1.tex"));

    auto abs = resolveToken("/usr/local/texlive/article.cls", workDir, resolver);
    ASSERT_TRUE(abs.has_value());
    EXPECT_EQ(abs->display, "/usr/local/texlive/article.cls");
    EXPECT_EQ(abs->abs, fs::path("/usr/local/texlive/article.cls"));

    auto rootPresent = makeRootRef("/project/main.tex", resolver);
    EXPECT_EQ(rootPresent.display, "main.tex");
    EXPECT_EQ(rootPresent.abs, fs::path("/project/main.tex"));

    auto rootMissing = makeRootRef("/project/absent.tex", resolver);
    EXPECT_EQ(rootMissing.display, "absent.tex");
    EXPECT_TRUE(rootMissing.abs.empty());

    auto fallbackRef = refForToken("./ghost.tex", workDir, resolver);
    EXPECT_EQ(fallbackRef.display, "ghost.tex");
    EXPECT_TRUE(fallbackRef.abs.empty());
}

TEST(DiagnosticsFileStack, isSourceEchoCases)
{
    const std::vector<std::string> lines {
        "  indented first line",
        "l.42 \\foo{(}",
        "            \\bar",
        "not indented after tag",
        "l.abc not a number",
        "  indented after non-tag",
        "l.",
    };

    EXPECT_FALSE(isSourceEcho(lines, 0));
    EXPECT_TRUE(isSourceEcho(lines, 1));
    EXPECT_TRUE(isSourceEcho(lines, 2));
    EXPECT_FALSE(isSourceEcho(lines, 3));
    EXPECT_FALSE(isSourceEcho(lines, 4));
    EXPECT_FALSE(isSourceEcho(lines, 5));
    EXPECT_FALSE(isSourceEcho(lines, 6));
    EXPECT_FALSE(isSourceEcho(lines, lines.size()));
}

TEST(DiagnosticsFileStack, parenTrackingQuotedPathsAndUnbalancedParens)
{
    FakeSourceResolver resolver;
    resolver.addFile("/project/main.tex");
    resolver.addFile("/project/my chapter.tex");
    resolver.addFile("/project/sub/sec.tex");

    FileStack stack("/project", "/project/main.tex", resolver);
    EXPECT_EQ(stack.current().display, "main.tex");

    // Extra closing parens on an empty stack are harmless.
    stack.scan(")))");
    EXPECT_EQ(stack.current().display, "main.tex");

    // Quoted path with spaces pushes properly.
    stack.scan("(./main.tex (\"./my chapter.tex\"");
    EXPECT_EQ(stack.current().display, "my chapter.tex");
    EXPECT_EQ(stack.current().abs, fs::path("/project/my chapter.tex"));

    // Unclosed quote falls back to token scan without crashing; non-file pushes sentinel.
    stack.scan("(\"./unterminated");
    EXPECT_EQ(stack.current().display, "my chapter.tex");
    stack.scan(")"); // pops the unclosed-quote sentinel
    EXPECT_EQ(stack.current().display, "my chapter.tex");

    // Nested real file, non-file version parens, and unbalanced inner non-file paren.
    stack.scan("(./sub/sec.tex (2024/01/01) (unclosed note");
    EXPECT_EQ(stack.current().display, "sub/sec.tex");
    stack.scan(")"); // pops "(unclosed"
    EXPECT_EQ(stack.current().display, "sub/sec.tex");
    stack.scan(")"); // pops sec.tex
    EXPECT_EQ(stack.current().display, "my chapter.tex");
    stack.scan(")"); // pops my chapter.tex
    EXPECT_EQ(stack.current().display, "main.tex");
    stack.scan(")))"); // pops main.tex + extra parens -> falls back to root
    EXPECT_EQ(stack.current().display, "main.tex");
}
