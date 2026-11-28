#include "maketex/Build.h"
#include "maketex/build/JobName.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using maketex::BuildOptions;
using maketex::EngineArguments;
using maketex::build::splitJobName;

TEST(JobNameTest, leavesOptionsUntouchedWithoutJobName)
{
    EngineArguments args {
        .program = "pdflatex",
        .extraOptions = { "-file-line-error", "-draftmode" },
    };

    auto [clean, job] = splitJobName(args);

    EXPECT_EQ(clean.program, "pdflatex");
    EXPECT_EQ(clean.extraOptions, (std::vector<std::string> { "-file-line-error", "-draftmode" }));
    EXPECT_FALSE(job.has_value());
}

TEST(JobNameTest, splitsEqualsForm)
{
    EngineArguments args {
        .extraOptions = { "-file-line-error", "-jobname=report", "-draftmode" },
    };

    auto [clean, job] = splitJobName(args);

    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(*job, "report");
    EXPECT_EQ(clean.extraOptions, (std::vector<std::string> { "-file-line-error", "-draftmode" }));
}

TEST(JobNameTest, splitsDoubleDashEqualsForm)
{
    EngineArguments args {
        .extraOptions = { "--jobname=thesis" },
    };

    auto [clean, job] = splitJobName(args);

    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(*job, "thesis");
    EXPECT_TRUE(clean.extraOptions.empty());
}

TEST(JobNameTest, splitsTwoArgumentForms)
{
    EngineArguments singleDash {
        .extraOptions = { "-a", "-jobname", "paper", "-b" },
    };
    auto [cleanSingle, jobSingle] = splitJobName(singleDash);
    ASSERT_TRUE(jobSingle.has_value());
    EXPECT_EQ(*jobSingle, "paper");
    EXPECT_EQ(cleanSingle.extraOptions, (std::vector<std::string> { "-a", "-b" }));

    EngineArguments doubleDash {
        .extraOptions = { "--jobname", "slides" },
    };
    auto [cleanDouble, jobDouble] = splitJobName(doubleDash);
    ASSERT_TRUE(jobDouble.has_value());
    EXPECT_EQ(*jobDouble, "slides");
    EXPECT_TRUE(cleanDouble.extraOptions.empty());
}

TEST(JobNameTest, keepsDanglingJobNameFlagWithoutValue)
{
    EngineArguments args {
        .extraOptions = { "-jobname" },
    };

    auto [clean, job] = splitJobName(args);

    EXPECT_FALSE(job.has_value());
    EXPECT_EQ(clean.extraOptions, (std::vector<std::string> { "-jobname" }));
}

TEST(JobNameTest, emptyJobNameValueResetsToNullopt)
{
    auto [cleanEq, jobEq] = splitJobName(EngineArguments { .extraOptions = { "-jobname=" } });
    EXPECT_FALSE(jobEq.has_value());
    EXPECT_TRUE(cleanEq.extraOptions.empty());

    auto [cleanTwo, jobTwo] = splitJobName(EngineArguments {
        .extraOptions = { "-jobname=first", "-jobname", "" },
    });
    EXPECT_FALSE(jobTwo.has_value());
    EXPECT_TRUE(cleanTwo.extraOptions.empty());
}

TEST(JobNameTest, lastJobNameWinsWhenRepeated)
{
    EngineArguments args {
        .extraOptions = { "-jobname=first", "--jobname", "second" },
    };

    auto [clean, job] = splitJobName(args);

    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(*job, "second");
    EXPECT_TRUE(clean.extraOptions.empty());
}

TEST(JobNameTest, jobNameForPrecedenceAndHelpers)
{
    BuildOptions opts {
        .texFile = "/project/doc/main.tex",
        .engineArguments = { .extraOptions = { "-jobname=fromflag" } },
    };
    EXPECT_EQ(maketex::jobNameFor(opts), "fromflag");

    opts.jobName = "explicit";
    EXPECT_EQ(maketex::jobNameFor(opts), "explicit");

    opts.jobName = "";
    EXPECT_EQ(maketex::jobNameFor(opts), "fromflag");

    opts.jobName.reset();
    opts.engineArguments.extraOptions.clear();
    EXPECT_EQ(maketex::jobNameFor(opts), "main");

    auto defaultPublished = maketex::publishedArtifactPaths(opts);
    ASSERT_EQ(defaultPublished.size(), 2u);
    EXPECT_EQ(defaultPublished[0], fs::path("/project/doc/main.pdf"));
    EXPECT_EQ(defaultPublished[1], fs::path("/project/doc/main.synctex.gz"));

    opts.outputDir = "/project/build";
    EXPECT_TRUE(maketex::publishedArtifactPaths(opts).empty());
    EXPECT_EQ(maketex::buildLockPath("/project/build", "main"),
        fs::path("/project/build/.main.lock"));
}
