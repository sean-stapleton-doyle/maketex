#include "IEngine.h"

#include "RunResult.h"

#include <gtest/gtest.h>

using namespace maketex;

namespace {

struct MockEngine : public IEngine {
    std::future<RunResult<EngineInvocation>> run(const EngineInvocation& inv) override
    {
        auto invPtr = std::make_shared<const EngineInvocation>(inv);
        return std::async(std::launch::deferred, [invPtr = std::move(invPtr)] {
            return RunResult<EngineInvocation> { .invocation = invPtr, .exitCode = 0 };
        });
    }

    std::string_view name() const override { return "mock"; }
    InvokerKind invokerKind() const override { return InvokerKind::PdfLatex; }
    bool handlesOwnLoop() const override { return false; }
};

static_assert(Engine<MockEngine>);

} // namespace

TEST(IEngine, mockSatisfiesConcept)
{
    MockEngine engine;
    EXPECT_EQ(engine.name(), "mock");
    EXPECT_FALSE(engine.handlesOwnLoop());
}

TEST(IEngine, mockRunReturnsSuccess)
{
    MockEngine engine;
    EngineInvocation inv { .texFile = "/a/main.tex", .outputDir = "/a", .auxDir = "/a" };
    auto result = engine.run(inv).get();
    EXPECT_TRUE(result.success());
}
