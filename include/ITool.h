#pragma once

#include "InvokerKind.h"
#include "RunDecision.h"
#include "ToolContext.h"
#include "ToolInvocation.h"

#include <concepts>
#include <string>
#include <string_view>
#include <vector>

namespace maketex {

template <typename T>
concept Tool = requires(T t, const ToolInvocation& inv, const ToolContext& ctx) {
    { t.name() } -> std::convertible_to<std::string_view>;
    { t.invokerKind() } -> std::same_as<InvokerKind>;
    { t.buildArgs(inv) } -> std::same_as<std::vector<std::string>>;
    { t.decide(ctx) } -> std::same_as<RunDecision>;
    { t.onSuccess(ctx) } -> std::same_as<void>;
};

// Note: decide() is non-const because tools may stash scheduling state
// computed during the check (e.g. a freshly-hashed trigger file) for
// onSuccess() to commit. Pure decision logic still reads through const refs.

// Auxiliary LaTeX tools (biber, bibtex, makeindex, etc.).
//
// Tools are pure descriptions of how/when to invoke an external program;
// execution is performed by ToolRunner, which composes an ITool with a
// tf::Executor. Adding a new tool means: subclass ITool, implement the
// hooks, register it with BuildDriver.
class ITool {
public:
    virtual ~ITool() = default;

    // Human-readable name, e.g. "bibtex".
    virtual std::string_view name() const = 0;

    // Kind tag stamped onto the ToolInvocation before launch. Used by
    // DiagnosticExtractor to dispatch to the right parser.
    virtual InvokerKind invokerKind() const = 0;

    // Returns the full argv. argv[0] is looked up via PATH.
    virtual std::vector<std::string> buildArgs(const ToolInvocation& inv) const = 0;

    // Returns how this tool should be handled this pass given post-pass context.
    virtual RunDecision decide(const ToolContext& ctx) = 0;

    // Called by BuildDriver after a successful run(). Tools that maintain
    // state across passes (e.g. "ran once already") update it here.
    virtual void onSuccess(const ToolContext& /*ctx*/) { }
};

} // namespace maketex
