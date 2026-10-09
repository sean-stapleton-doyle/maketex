#include "maketex/PathCodec.h"

#include <gtest/gtest.h>

namespace fs = std::filesystem;
using maketex::PathCodec;

TEST(PathCodec, defaultConstructedIsIdentity)
{
    PathCodec codec;
    EXPECT_EQ(codec.encode("/abs/anywhere/file.tex"), "/abs/anywhere/file.tex");
    EXPECT_EQ(codec.decode("/abs/anywhere/file.tex"),
        fs::path("/abs/anywhere/file.tex"));
}

TEST(PathCodec, pathsUnderRootStoreRelative)
{
    PathCodec codec { "/proj" };
    EXPECT_EQ(codec.encode("/proj/main.tex"), "main.tex");
    EXPECT_EQ(codec.encode("/proj/chapters/ch1.tex"), "chapters/ch1.tex");
}

TEST(PathCodec, pathsOutsideRootStayAbsolute)
{
    PathCodec codec { "/proj" };
    EXPECT_EQ(codec.encode("/usr/share/texmf/article.cls"),
        "/usr/share/texmf/article.cls");
    EXPECT_EQ(codec.encode("/proj-sibling/file.tex"), "/proj-sibling/file.tex");
}

TEST(PathCodec, rootItselfStaysAbsolute)
{
    PathCodec codec { "/proj" };
    EXPECT_EQ(codec.encode("/proj"), "/proj");
}

TEST(PathCodec, decodeReanchorsRelativeOnRoot)
{
    PathCodec codec { "/proj" };
    EXPECT_EQ(codec.decode("chapters/ch1.tex"),
        fs::path("/proj/chapters/ch1.tex"));
}

TEST(PathCodec, decodePassesAbsoluteThrough)
{
    PathCodec codec { "/proj" };
    EXPECT_EQ(codec.decode("/usr/share/texmf/article.cls"),
        fs::path("/usr/share/texmf/article.cls"));
}

TEST(PathCodec, roundTripAfterRootMove)
{
    // The portability contract: what one codec encoded under the old root,
    // a codec with the new root decodes to the moved location.
    PathCodec before { "/ci/runner-1/checkout" };
    PathCodec after { "/ci/runner-2/work" };
    auto stored = before.encode("/ci/runner-1/checkout/main.tex");
    EXPECT_EQ(after.decode(stored), fs::path("/ci/runner-2/work/main.tex"));
}

TEST(PathCodec, dotSegmentsNormaliseInEncoding)
{
    PathCodec codec { "/proj" };
    EXPECT_EQ(codec.encode("/proj/./chapters/../main.tex"), "main.tex");
}
