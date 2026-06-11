#pragma once

#include <functional>
#include <memory>

namespace maketex {

struct AuxRecord;
struct EngineInvocation;
struct LogRecord;

// Post-pass context passed to ITool::decide() and ITool::onSuccess().
//
// Log and aux records are produced lazily — the supplied sources are invoked
// the first time log()/aux() is called and the result is cached. Tools that
// don't read these fields pay no parsing cost, and headers including
// ToolContext.h don't transitively pull in the parser layer.
class ToolContext {
public:
    using LogSource = std::function<LogRecord()>;
    using AuxSource = std::function<AuxRecord()>;

    ToolContext(const EngineInvocation& inv,
        LogSource logSource,
        AuxSource auxSource);
    ~ToolContext();

    ToolContext(const ToolContext&) = delete;
    ToolContext& operator=(const ToolContext&) = delete;

    const EngineInvocation& inv() const { return m_inv; }

    const LogRecord& log() const;
    const AuxRecord& aux() const;

private:
    const EngineInvocation& m_inv;
    LogSource m_logSource;
    AuxSource m_auxSource;
    mutable std::unique_ptr<LogRecord> m_log;
    mutable std::unique_ptr<AuxRecord> m_aux;
};

} // namespace maketex
