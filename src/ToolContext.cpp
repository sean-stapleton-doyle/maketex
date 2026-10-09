#include "maketex/ToolContext.h"

#include "maketex/parsers/AuxParser.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"

#include <algorithm>
#include <utility>

namespace maketex {

namespace fs = std::filesystem;

ToolContext::ToolContext(const EngineInvocation& inv,
    LogSource logSource,
    AuxSource auxSource,
    FlsSource flsSource)
    : m_inv(inv)
    , m_logSource(std::move(logSource))
    , m_auxSource(std::move(auxSource))
    , m_flsSource(std::move(flsSource))
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

const FlsRecord& ToolContext::fls() const
{
    if (!m_fls)
        m_fls = std::make_unique<FlsRecord>(m_flsSource());
    return *m_fls;
}

bool ToolContext::wrote(const fs::path& path) const
{
    // FlsRecord paths are weakly canonical (see FlsParser).
    auto canonical = fs::weakly_canonical(path);
    return std::ranges::find(fls().outputs, canonical) != fls().outputs.end();
}

} // namespace maketex
