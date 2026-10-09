#pragma once

#include <filesystem>
#include <functional>
#include <memory>

namespace maketex {

struct AuxRecord;
struct EngineInvocation;
struct FlsRecord;
struct LogRecord;

// What the latest engine pass left behind, passed to ITool's hooks.
//
// Log, aux, and fls records are produced lazily — each source is invoked the
// first time its record is requested and the result is cached. Tools that
// don't read a record pay no parsing cost, and headers including
// ToolContext.h don't transitively pull in the parser layer.
class ToolContext {
public:
    using LogSource = std::function<LogRecord()>;
    using AuxSource = std::function<AuxRecord()>;
    using FlsSource = std::function<FlsRecord()>;

    ToolContext(const EngineInvocation& inv,
        LogSource logSource,
        AuxSource auxSource,
        FlsSource flsSource);
    ~ToolContext();

    ToolContext(const ToolContext&) = delete;
    ToolContext& operator=(const ToolContext&) = delete;

    const EngineInvocation& inv() const { return m_inv; }
    const LogRecord& log() const;
    const AuxRecord& aux() const;
    const FlsRecord& fls() const;

    // True when the latest pass wrote path (an OUTPUT line in its .fls).
    bool wrote(const std::filesystem::path& path) const;

private:
    const EngineInvocation& m_inv;
    LogSource m_logSource;
    AuxSource m_auxSource;
    FlsSource m_flsSource;
    mutable std::unique_ptr<LogRecord> m_log;
    mutable std::unique_ptr<AuxRecord> m_aux;
    mutable std::unique_ptr<FlsRecord> m_fls;
};

} // namespace maketex
