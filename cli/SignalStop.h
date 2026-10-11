#pragma once

#include <atomic>
#include <csignal>
#include <stop_token>
#include <thread>

namespace maketex::cli {

// Turns SIGINT, SIGTERM, and SIGHUP into a stop request, so stopping maketex
// stops the engine or tool it is running instead of orphaning it (TeX would
// otherwise keep running — forever, for a document that loops).
//
// The signals are blocked in every thread (construct this before starting
// any) and received by a dedicated thread with sigwait: a signal handler may
// not touch a stop_source. A second signal exits at once, for a user who
// doesn't want to wait for cleanup. Child processes start with the default
// signal state (see Process), so the engine still responds to SIGTERM.
class SignalStop {
public:
    SignalStop();
    ~SignalStop();

    SignalStop(const SignalStop&) = delete;
    SignalStop& operator=(const SignalStop&) = delete;

    std::stop_token token() const { return m_source.get_token(); }

    // The signal that requested the stop, or 0.
    int received() const { return m_signal.load(); }

private:
    std::stop_source m_source;
    std::atomic<int> m_signal { 0 };
    std::atomic<bool> m_closing { false };
    sigset_t m_previous { };
    std::thread m_thread;
};

} // namespace maketex::cli
