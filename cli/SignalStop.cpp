#include "SignalStop.h"

#include <pthread.h>
#include <unistd.h>

namespace maketex::cli {

namespace {

    sigset_t handledSignals()
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGHUP);
        sigaddset(&set, SIGUSR1); // our own wake-up, to shut the thread down
        return set;
    }

} // namespace

SignalStop::SignalStop()
{
    auto set = handledSignals();
    ::pthread_sigmask(SIG_BLOCK, &set, &m_previous);
    m_thread = std::thread([this, set] {
        while (true) {
            int sig = 0;
            if (::sigwait(&set, &sig) != 0)
                continue;
            if (sig == SIGUSR1) {
                if (m_closing)
                    return;
                continue;
            }
            if (m_signal.exchange(sig) != 0)
                ::_exit(128 + sig); // the second signal: stop waiting
            m_source.request_stop();
        }
    });
}

SignalStop::~SignalStop()
{
    m_closing = true;
    ::pthread_kill(m_thread.native_handle(), SIGUSR1);
    m_thread.join();
    ::pthread_sigmask(SIG_SETMASK, &m_previous, nullptr);
}

} // namespace maketex::cli
