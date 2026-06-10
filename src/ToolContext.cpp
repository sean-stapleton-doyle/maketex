#include "ToolContext.h"

#include "parsers/AuxParser.h"
#include "parsers/LogParser.h"

#include <utility>

namespace maketex {

ToolContext::ToolContext(const EngineInvocation& inv,
    LogSource logSource,
    AuxSource auxSource)
    : m_inv(inv)
    , m_logSource(std::move(logSource))
    , m_auxSource(std::move(auxSource))
{
}

ToolContext::~ToolContext() = default;

const LogRecord& ToolContext::log() const
{
    if (!m_log)
        m_log = std::make_unique<LogRecord>(m_logSource());
    return *m_log;
}

const AuxRecord& ToolContext::aux() const
{
    if (!m_aux)
        m_aux = std::make_unique<AuxRecord>(m_auxSource());
    return *m_aux;
}

} // namespace maketex
