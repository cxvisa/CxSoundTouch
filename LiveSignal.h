#ifndef LIVE_SIGNAL_H
#define LIVE_SIGNAL_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

// Lets one thread tell others that something live has changed, so that a dashboard request waiting
// on it can answer at once rather than at its next poll. Each change moves a sequence number on; a
// waiter passes the last one it saw and returns as soon as it has moved past that.
class LiveSignal
{
    public :

        LiveSignal ()
            : m_seq (0)
        {
        }

        LiveSignal (const LiveSignal &) = delete;
        LiveSignal &operator= (const LiveSignal &) = delete;

        // Something changed: move the sequence on and wake every waiter.
        void notify ()
        {
            {
                std::lock_guard<std::mutex> lock (m_mutex);

                ++m_seq;
            }

            m_changed.notify_all ();
        }

        std::uint64_t seq () const
        {
            std::lock_guard<std::mutex> lock (m_mutex);

            return (m_seq);
        }

        // Waits until the sequence has moved past seen, timeout passes, or abandon is set (and
        // wakeAll called); returns the sequence as it is then.
        std::uint64_t waitPast (std::uint64_t seen, std::chrono::milliseconds timeout, const std::atomic<bool> &abandon)
        {
            std::unique_lock<std::mutex> lock (m_mutex);

            m_changed.wait_for (lock, timeout, [&] () { return (m_seq != seen || abandon.load ()); });

            return (m_seq);
        }

        // Wakes every waiter without a change, so that one whose abandon flag has just been set
        // returns now rather than at its timeout. Set the flag first.
        void wakeAll ()
        {
            std::lock_guard<std::mutex> lock (m_mutex);

            m_changed.notify_all ();
        }

    private :

        // Now the data members

        mutable std::mutex      m_mutex;
        std::condition_variable m_changed;
        std::uint64_t           m_seq;
};

#endif
