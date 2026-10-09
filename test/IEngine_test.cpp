#include "maketex/IEngine.h"

#include "maketex/RunResult.h"

#include <gtest/gtest.h>

using namespace maketex;

namespace {

struct MockEngine : public IEngine {
    RunResult<EngineInvocation> run(const EngineInvocation& inv, const maketex::WaitLimits&) override
    {
        return RunResult<EngineInvocation> { .invocation = inv, .exitCode = 0 };
    }

    std::string_view name() const override { return "mock"; }
    InvokerKind invokerKind() const override { return InvokerKind::PdfLaTeX; }
};

} // namespace

TEST(IEngine, mockImplementsInterface)
{
    MockEngine engine;
    EXPECT_EQ(engine.name(), "mock");
}

TEST(IEngine, mockRunReturnsSuccess)
{
    MockEngine engine;
    EngineInvocation inv { .texFile = "/a/main.tex", .outputDir = "/a" };
    auto result = engine.run(inv, { });
    EXPECT_TRUE(result.success());
}
