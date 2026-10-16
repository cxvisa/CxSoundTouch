#ifndef ICY_READER_H
#define ICY_READER_H

#include "IcyDemuxer.h"
#include <string>
#include <functional>
#include <cstddef>

// Reads the song titles a Shoutcast-style stream interleaves with its audio. The speaker never
// asks for these, so this opens its own connection and leaves playback untouched.
class IcyReader
{
    public :

        // Called for each new title. Return false to stop reading.
        using TitleCallback = IcyDemuxer::TitleCallback;

        // Polled while the connection is open: often while data flows, and at least once a
        // second. Return true to stop reading; this is how a long-held connection notices a
        // station change or shutdown.
        using AbortCheck = std::function<bool()>;

        explicit IcyReader (TitleCallback onTitle, AbortCheck shouldAbort = AbortCheck ());

        // timeoutSeconds caps the whole read; 0 means no cap, for watching indefinitely.
        bool read (const std::string &streamUrl, long timeoutSeconds = 0);

        const std::string &getError () const { return (m_error); }

        // The station answered but sends no titles, so reading it again is pointless.
        bool lacksMetadata () const { return (m_lacksMetadata); }

    private :

        static size_t headerTrampoline (char *buffer, size_t size, size_t items, void *userp);
        static size_t bodyTrampoline (char *buffer, size_t size, size_t items, void *userp);

        size_t onBody (const char *buffer, size_t length);

        // Now the data members

        IcyDemuxer    m_demuxer;
        AbortCheck    m_shouldAbort;
        std::string   m_error;
        bool          m_checkedMetadata;
        bool          m_lacksMetadata;
        bool          m_stopped;
};

#endif
