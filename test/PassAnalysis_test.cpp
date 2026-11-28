#include "maketex/driver/PassAnalysis.h"

#include "maketex/EngineInvocation.h"
#include "maketex/graph/BuildGraph.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

using maketex::BuildGraph;
using maketex::EngineInvocation;
using maketex::FileNode;
using maketex::FileRole;
using maketex::Hash;
using maketex::InvokerKind;
using maketex::RunResult;
using maketex::driver::Digests;
using maketex::driver::failureMessage;
using maketex::driver::firstChangedReadBack;
using maketex::driver::formatRerunReason;
using maketex::driver::isFinalProduct;
using maketex::driver::joinRerunReasons;
using maketex::driver::PassSignals;
using maketex::driver::rerunReasons;

namespace {

Hash makeHash(std::uint8_t fill)
{
    Hash h { };
    h.fill(fill);
    return h;
}

} // namespace

TEST(PassAnalysisTest, isFinalProductClassifiesKnownExtensions)
{
    for (const char* p : { "/project/main.pdf", "/project/main.dvi", "/project/main.xdv",
             "/project/main.log", "/project/main.fls", "/project/main.synctex.gz",
             "/project/main.synctex", "/project/main.run.xml" }) {
        EXPECT_TRUE(isFinalProduct(p)) << p;
    }
    for (const char* p : { "/project/main.tex", "/project/main.aux", "/project/main.toc",
             "/project/main.bbl", "/project/main.idx", "/project/main.bcf" }) {
        EXPECT_FALSE(isFinalProduct(p)) << p;
    }
}

TEST(PassAnalysisTest, failureMessageCoversAllOutcomeKinds)
{
    RunResult<EngineInvocation> r {
        .invocation = EngineInvocation { },
        .kind = InvokerKind::PdfLaTeX,
        .exitCode = 127,
        .program = "biber",
        .notFound = true,
    };
    EXPECT_EQ(failureMessage("biber", r), "biber is not installed (not found on PATH)");

    r.notFound = false;
    r.cancelled = true;
    EXPECT_EQ(failureMessage("latex", r), "cancelled");

    r.cancelled = false;
    r.timedOut = true;
    EXPECT_EQ(failureMessage("latex", r), "latex timed out");

    r.timedOut = false;
    r.exitCode = 2;
    EXPECT_EQ(failureMessage("xdvipdfmx", r), "xdvipdfmx exited with code 2");
}

TEST(PassAnalysisTest, firstChangedReadBackIgnoresNonGeneratedAndNewlyCreatedFiles)
{
    FileNode src("/project/main.tex", FileRole::Source);
    FileNode out("/project/main.idx", FileRole::Output);
    FileNode newlyWritten("/project/main.toc", FileRole::Generated);
    FileNode unchanged("/project/main.aux", FileRole::Generated);
    std::vector<const FileNode*> files { &src, &out, &newlyWritten, &unchanged };

    Digests before {
        { "/project/main.tex", makeHash(1) },
        { "/project/main.idx", makeHash(1) },
        { "/project/main.aux", makeHash(7) },
    };
    Digests after {
        { "/project/main.tex", makeHash(2) },
        { "/project/main.idx", makeHash(2) },
        { "/project/main.toc", makeHash(3) },
        { "/project/main.aux", makeHash(7) },
    };

    EXPECT_FALSE(firstChangedReadBack(files, before, after).has_value());
}

TEST(PassAnalysisTest, firstChangedReadBackDetectsModifiedAndDeletedGeneratedFiles)
{
    FileNode aux("/project/main.aux", FileRole::Generated);
    std::vector<const FileNode*> files { &aux };

    Digests before { { "/project/main.aux", makeHash(1) } };
    Digests modified { { "/project/main.aux", makeHash(2) } };
    Digests deleted { };

    auto changed = firstChangedReadBack(files, before, modified);
    ASSERT_TRUE(changed.has_value());
    EXPECT_EQ(*changed, "/project/main.aux");

    auto removed = firstChangedReadBack(files, before, deleted);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(*removed, "/project/main.aux");
}

TEST(PassAnalysisTest, rerunReasonsEmptyWhenNoSignalsAndNotDraftFirstPass)
{
    EXPECT_TRUE(rerunReasons(PassSignals { }).empty());
    EXPECT_TRUE(rerunReasons(PassSignals { .pass = 1, .draftFirstPass = true }).empty());
    EXPECT_TRUE(rerunReasons(PassSignals { .pass = 0, .draftFirstPass = true, .xdvSplit = true }).empty());
}

TEST(PassAnalysisTest, rerunReasonsEmitsDraftFinalizationOnlyWhenSilentOnPassZero)
{
    auto draftOnly = rerunReasons(PassSignals { .pass = 0, .draftFirstPass = true });
    EXPECT_EQ(draftOnly, (std::vector<std::string> { "emitting final PDF after draft pass" }));

    auto draftWithSignal = rerunReasons(PassSignals {
        .pass = 0,
        .logAskedRerun = true,
        .draftFirstPass = true,
    });
    EXPECT_EQ(draftWithSignal, (std::vector<std::string> { "LaTeX asked for a rerun" }));
}

TEST(PassAnalysisTest, rerunReasonsPreservesCanonicalOrderAndFormatting)
{
    PassSignals s {
        .pass = 0,
        .logAskedRerun = true,
        .readBackChanged = "/project/.maketex/main.aux",
        .appearedInput = "main.toc now exists",
        .editedDuringPass = "/project/chapters/ch1.tex",
        .toolChanges = { "biber rewrote main.bbl", "makeindex rewrote main.ind" },
        .draftFirstPass = true,
    };
    auto reasons = rerunReasons(s);
    EXPECT_EQ(reasons, (std::vector<std::string> {
                           "LaTeX asked for a rerun",
                           "main.aux changed",
                           "main.toc now exists",
                           "ch1.tex was saved during the pass",
                           "biber rewrote main.bbl",
                           "makeindex rewrote main.ind",
                       }));
    EXPECT_EQ(joinRerunReasons(reasons),
        "LaTeX asked for a rerun; main.aux changed; main.toc now exists; "
        "ch1.tex was saved during the pass; biber rewrote main.bbl; makeindex rewrote main.ind");
    EXPECT_EQ(formatRerunReason(2, reasons),
        "pass 2: LaTeX asked for a rerun; main.aux changed; main.toc now exists; "
        "ch1.tex was saved during the pass; biber rewrote main.bbl; makeindex rewrote main.ind");
}
