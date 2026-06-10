#pragma once

#include "EngineInvocation.h"
#include "InvokerKind.h"
#include "RunResult.h"

#include <concepts>
#include <future>
#include <string_view>

namespace maketex {

template <typename T>
concept Engine = requires(T e, const EngineInvocation& inv) {
    { e.run(inv) } -> std::same_as<std::future<RunResult<EngineInvocation>>>;
    { e.name() } -> std::convertible_to<std::string_view>;
    { e.invokerKind() } -> std::same_as<InvokerKind>;
    { e.handlesOwnLoop() } -> std::same_as<bool>;
};

class IEngine {
public:
    virtual ~IEngine() = default;

    virtual std::future<RunResult<EngineInvocation>> run(const EngineInvocation&) = 0;

    // Human-readable name, e.g. "pdflatex", "xelatex".
    virtual std::string_view name() const = 0;

    // Kind tag stamped onto RunResult before delivery — used by
    // DiagnosticExtractor to dispatch to the right log parser.
    virtual InvokerKind invokerKind() const = 0;

    // True for engines that run their own multi-pass loop (e.g. tectonic).
    // When true, the driver skips its fixed-point outer loop.
    virtual bool handlesOwnLoop() const = 0;
};

} // namespace maketex
