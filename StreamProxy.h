#ifndef STREAM_PROXY_H
#define STREAM_PROXY_H

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdint>

class IcyDemuxer;

// A local HTTP relay that shortens the silence when the speaker is handed a stream, and knows
// which song the speaker is actually hearing.
//
// Faster: the upstream connection is already open and flowing, so the speaker never waits on
// DNS, a redirect, a TLS handshake or the CDN's own start-up; and a backlog of audio is already
// buffered here, so it is delivered at LAN speed instead of trickling in at the stream's
// bitrate. Measured on a SoundTouch 30: ~4.0s direct, ~2.6s relayed live, ~0.7s relayed with
// the backlog.
//
// Song-aware: the relay asks the station for its ICY titles on the same connection, strips
// them out of the audio (the speaker never asks for them), and records the stream byte where
// each title takes effect. From where the speaker's connection started and the stream's byte
// rate it estimates the byte being played now, and so when a new song is really heard.
//
// Seamless: the station opens every connection with a backlog of audio already sent, so after a
// dropped connection the relay finds where the new one catches up and drops the repeat.
//
// Stream positions are byte offsets that keep counting up across every station the relay
// carries; a generation number identifies each station.
class StreamProxy
{
    public :

        struct TitleMark
        {
            std::uint64_t offset;      // first stream byte the title applies to
            std::string   title;
        };

        // Where a speaker that has fallen further behind than the relay holds starts again, and
        // the title in force there.
        struct CatchUp
        {
            std::uint64_t start;
            TitleMark     mark;
        };

        // Where a press of the remote's next or previous button takes the speaker.
        struct Skip
        {
            std::uint64_t start;       // where it carries on
            TitleMark     mark;        // the title in force there
            int           songs;       // how many songs on (next) or back (previous); 0 for none
                                       // on, or back to the start of the song it was in
            bool          partial;     // that song began before the relay joined the station
            bool          catchUp;     // it was further behind than the relay holds: caught up
        };

        // Everything needed to decide whether a title is due, read under one lock.
        struct Timeline
        {
            std::uint64_t          generation;
            std::uint64_t          written;           // stream bytes received so far
            long                   bytesPerSecond;    // 0 until known
            bool                   speakerAttached;   // the speaker is streaming this generation
            bool                   speakerStopped;    // ...but a preset press has stopped it
            std::uint64_t          speakerPosition;   // estimated byte it is playing now
            std::uint64_t          speakerConnections; // how many it has opened, ever
            std::chrono::steady_clock::time_point speakerConnectedAt; // when the newest one began
            std::vector<TitleMark> upcoming;          // titles after the asked offset, oldest first

            // A speaker can be started no further back than resumeFloor; one that would have to
            // be starts at catchUp instead.
            std::uint64_t          resumeFloor;
            CatchUp                catchUp;

            // The speaker asked to carry on from where it paused, which the relay no longer
            // holds; it is waiting to be started again. A speaker still paused asks too, when the
            // connection it held open is dropped.
            bool                   speakerNeedsRestart;

            // False once the station's headers show it sends no titles.
            bool                   titlesOffered;
        };

        // How long a pause can last and still carry on where it stopped, in whole minutes, with this
        // buffer at this rate: all of it but RESUME_MARGIN, counted from what the speaker last had.
        static long pauseMinutes (size_t bufferBytes, long bytesPerSecond)
        {
            return (static_cast<long> ((bufferBytes - RESUME_MARGIN) / static_cast<size_t> (bytesPerSecond) / 60));
        }

        // The history the relay keeps unless told otherwise, and the range it may be given.
        static constexpr size_t DEFAULT_BUFFER_MB = 16;
        static constexpr size_t MIN_BUFFER_MB     = 2;
        static constexpr size_t MAX_BUFFER_MB     = 512;

        StreamProxy ();
        ~StreamProxy ();

        StreamProxy (const StreamProxy &) = delete;
        StreamProxy &operator= (const StreamProxy &) = delete;

        // Only connections from speakerIp are tracked as the speaker; anyone else is served but
        // does not move the playback estimate. bufferBytes is how much of the stream is kept, which
        // is how long a pause can still carry on exactly where it stopped.
        bool start (int port, const std::string &speakerIp, size_t bufferBytes = DEFAULT_BUFFER_MB * 1024 * 1024);
        void stop ();
        bool isRunning () const { return (m_running); }

        // Re-points the upstream and returns the generation that now identifies it. The same URL
        // again keeps the current generation and its buffer.
        std::uint64_t setUpstream (const std::string &url);

        // The URL to hand the speaker. The stream name rides in the path only so that /nowPlaying
        // still says which station is on; the relay always serves the current upstream.
        std::string urlForSpeaker (const std::string &streamName) const;

        // The reverse: the stream name from a relay URL on this port (any port when 0), or "" if it
        // is not one.
        static std::string streamNameFromUrl (const std::string &url, int port);

        // Waits, up to timeout, for the station's first title and the rest of its opening
        // backlog, then gives the title to show and, in startOffset, where the speaker should
        // start (0 for the usual backlog):
        //  - If the speaker was on this station moments ago, a re-press, it carries on where it
        //    stopped (resumes is set), under the title in force there. That title is empty for a
        //    station that sends none.
        //  - Otherwise the title is the newest, the song playing now. When that song began inside
        //    the backlog and enough of it is here to start on promptly, the speaker starts on it,
        //    skipping the tail of the song before.
        // False if there is neither a title nor a place to resume, or the generation moved on.
        bool waitForTitle (std::uint64_t generation, std::chrono::milliseconds timeout,
                           TitleMark &mark, std::uint64_t &startOffset, bool &resumes);

        // A preset button was pressed, which stops the speaker's playback at once, or it was paused
        // at the given moment. Its position is held there until it next connects, so a re-press
        // carries on from that point, and no song is pushed to a speaker that has stopped.
        void noteSpeakerStopped (std::chrono::steady_clock::time_point at);
        void noteSpeakerStopped () { noteSpeakerStopped (std::chrono::steady_clock::now ()); }

        // The speaker has gone back to playing on the same connection, as after a pause from the
        // remote: its position carries on from where it was held.
        void noteSpeakerResumed ();

        // Titles are the ones strictly after the given offset.
        Timeline getTimeline (std::uint64_t generation, std::uint64_t after) const;

        // How many connections the speaker has opened, ever: a new one after a pause means it was
        // played again.
        std::uint64_t speakerConnections () const;

        // Where the remote's next (steps > 0) or previous (steps < 0) button takes the speaker from
        // where it is, where it stopped, or where it was last sent and has not got to yet; with
        // steps 0, as when presses cancelled out, it carries on. Every title change counts as a song:
        //  - Next goes to the start of a later song, once MIN_START_LEAD_SECONDS of it has arrived;
        //    with none yet, it carries on where it was.
        //  - Previous first goes back to the start of the song it is in, if it is
        //    SKIP_RESTART_SECONDS or more into it and this is not a press straight after another
        //    (again), as players do; then to the songs before, as far back as the history reaches.
        // False when there is no telling where the speaker is: not on this station, or no rate.
        bool skipFrom (std::uint64_t generation, int steps, bool again, Skip &skip) const;

        // The speaker's next connection starts at offset instead of the usual backlog, so a
        // re-issued URL carries on exactly where it stopped: nothing skipped, nothing repeated.
        // Also answers a speaker that is waiting to be started again.
        void resumeAt (std::uint64_t generation, std::uint64_t offset);

        // A button press has the speaker now: it is no longer waiting to be started again.
        void cancelRestart ();

        // How far from a station's title change its songs really begin, as control's --title-offset
        // says (negative: the station changes the title that long after the song starts). Starting
        // the speaker on a song, as a catch-up or a skip does, starts it there.
        void setTitleOffset (double seconds);
        void cancelResume ();

    private :

        struct Client
        {
            std::thread       thread;
            int               fd;
            std::uint64_t     sequence;         // accept order
            bool              fromSpeaker;
            std::atomic<bool> done;

            Client (int socket, std::uint64_t order, bool speaker)
                : fd (socket),
                  sequence (order),
                  fromSpeaker (speaker),
                  done (false)
            {
            }
        };

        struct HeldTitle
        {
            size_t      at;                     // byte within the held audio
            std::string title;
        };

        // One upstream connection. A reconnect to the same station holds its audio back until it
        // is lined up with what was already received.
        struct PumpContext
        {
            StreamProxy            *self;
            IcyDemuxer             *demuxer;
            std::uint64_t           generation;
            bool                    published;
            bool                    splicing;
            std::vector<char>       spliceKey;  // the last bytes received before the drop
            std::vector<char>       held;
            std::vector<HeldTitle>  heldTitles;

            // How far the held audio has got ahead of real time, and when that last grew.
            std::chrono::steady_clock::time_point holdStart;
            std::chrono::steady_clock::time_point holdGrew;
            double                                holdExcess;
        };

        void pumpLoop ();
        void acceptLoop ();
        void serveClient (Client *client);
        void reapClients (bool all);

        void pumpAudio (PumpContext &context, const char *data, size_t length);
        void pumpTitle (PumpContext &context, const std::string &title);
        void finishSplice (PumpContext &context, size_t from);

        void append (std::uint64_t generation, const char *data, size_t length);
        void addTitle (std::uint64_t generation, const std::string &title);
        void publishHeaders (std::uint64_t generation, const IcyDemuxer &demuxer);
        void copyOut (std::uint64_t position, size_t length, char *destination) const;

        long          bytesPerSecondLocked () const;
        double        burstExcessLocked (long bytesPerSecond) const;
        std::uint64_t speakerEstimateLocked (std::chrono::steady_clock::time_point now) const;
        void          holdSpeakerClockLocked (std::chrono::steady_clock::time_point now);
        std::uint64_t oldestLocked () const;
        std::uint64_t backlogStartLocked () const;
        std::uint64_t resumeFloorLocked () const;
        TitleMark     titleAtLocked (std::uint64_t position) const;
        CatchUp       catchUpLocked () const;
        std::uint64_t songStartLocked (size_t index) const;
        std::uint64_t speakerReachedLocked (std::uint64_t generation, std::chrono::steady_clock::time_point now,
                                            bool anyAge = false) const;

        static long   standardBitrate (long kbps);
        static size_t curlHeader (char *buffer, size_t size, size_t items, void *userp);
        static size_t curlWrite (char *buffer, size_t size, size_t items, void *userp);

        // Now the data members

        std::unique_ptr<char[]>              m_ring;             // allocated by start()
        size_t                               m_ringBytes;
        std::uint64_t                        m_written;          // total bytes ever received
        std::uint64_t                        m_generationStart;  // first byte of this station
        std::string                          m_upstream;
        std::atomic<std::uint64_t>           m_generation;       // changed only under m_mutex
        std::deque<TitleMark>                m_titles;           // this generation, oldest first

        std::string                          m_contentType;
        std::uint64_t                        m_headersGeneration;
        bool                                 m_metadataOffered;  // icy-metaint in those headers
        long                                 m_metaInterval;     // ...and its value
        long                                 m_bitrateKbps;      // icy-br, snapped to a standard rate
        long                                 m_measuredRate;     // bytes/s, 0 until measured
        bool                                 m_rateBaseSet;
        std::uint64_t                        m_rateBaseBytes;
        std::uint64_t                        m_connectionStart;  // m_written at the first byte
        std::chrono::steady_clock::time_point m_connectedAt;
        std::chrono::steady_clock::time_point m_rateBaseTime;

        const Client                        *m_speakerClient;    // the speaker's live connection
        std::uint64_t                        m_latestSpeakerSequence;
        std::uint64_t                        m_speakerGeneration;
        std::uint64_t                        m_speakerStart;
        std::uint64_t                        m_speakerSent;      // sent on its live connection
        std::chrono::steady_clock::time_point m_speakerSince;
        bool                                 m_speakerStopped;   // held at m_speakerStoppedAt
        std::uint64_t                        m_speakerStoppedAt; // 0 if the rate was not known
        std::chrono::steady_clock::time_point m_speakerStoppedTime;

        // Where byte 0 of the speaker's current stream sits in ours. After a pause it asks for
        // "Range: bytes=N-" of that same stream, so this is what turns N back into a position.
        std::int64_t                         m_speakerOrigin;
        std::uint64_t                        m_speakerOriginGeneration;

        // Where the speaker had got to when its last connection ended, for a re-press; 0 if the
        // rate was not known, when there is no telling.
        std::uint64_t                        m_speakerLeftAt;
        std::uint64_t                        m_speakerLeftGeneration;
        std::chrono::steady_clock::time_point m_speakerLeftTime;

        bool                                 m_resumePending;
        std::uint64_t                        m_resumeGeneration;
        std::uint64_t                        m_resumeOffset;

        // Where the speaker was last sent, until it gets there: a press that comes before then
        // counts from there, not from where it was before.
        bool                                 m_intendedPending;
        std::uint64_t                        m_intendedAt;
        std::uint64_t                        m_intendedGeneration;
        std::chrono::steady_clock::time_point m_intendedTime;

        std::uint64_t                        m_speakerConnections;
        double                               m_titleOffsetSeconds;

        // Played again while it had no connection, and when.
        bool                                 m_resumedWhileGone;
        std::chrono::steady_clock::time_point m_resumedWhileGoneTime;
        std::chrono::steady_clock::time_point m_speakerConnectedAt;

        // The speaker asked to carry on from bytes no longer held, and waits to be started again.
        bool                                 m_restartPending;
        std::uint64_t                        m_restartGeneration;

        std::string                          m_speakerIp;
        int                                  m_port;
        int                                  m_listenFd;
        std::atomic<bool>                    m_running;
        std::atomic<bool>                    m_stopping;
        std::thread                          m_pumpThread;
        std::thread                          m_acceptThread;
        std::vector<std::unique_ptr<Client>> m_clients;          // accept thread and stop() only
        std::uint64_t                        m_acceptSequence;   // accept thread only
        mutable std::mutex                   m_mutex;
        std::condition_variable              m_changed;

        // The history (m_ringBytes) is what lets a song change, a re-press or a pause carry on
        // exactly where the speaker was. Each song change adds its silence (~1.2s) to how far the
        // speaker trails the station, and each pause its whole length. A speaker can be started
        // anywhere in the history except its oldest RESUME_MARGIN, which is about to be
        // overwritten while it reads; further back than that it catches up instead.
        static constexpr size_t RESUME_MARGIN = 512 * 1024;

        // A catch-up starts on a song that has been on at least this long, which also means it is
        // not a station ID between songs (main's MIN_TITLE_SECONDS).
        static constexpr long CATCH_UP_LEAD_SECONDS = 10;

        // Previous restarts the song the speaker is in when it is at least this far into it, and
        // goes to the song before when it is not.
        static constexpr long SKIP_RESTART_SECONDS = 5;

        // A skip back to a song whose start is no longer held lands this far inside what is, so the
        // speaker is not at the edge, about to be caught up, the moment it starts; and a speaker
        // that has slipped this much past the edge since counts as at it.
        static constexpr long FLOOR_LANDING_SECONDS = 10;

        // Stream positions start here, not at 0, so that 0 can mean "not known" wherever it does.
        static constexpr std::uint64_t STREAM_BASE = 1;

        // A reconnect that has not lined up with what was received by this much has lost audio.
        static constexpr size_t SPLICE_HOLD_LIMIT = 1536 * 1024;

        // What a fresh connection is given at LAN speed: ~12s at 64 kbps.
        static constexpr size_t BURST_BYTES = 96 * 1024;

        static constexpr size_t SEND_CHUNK  = 16 * 1024;

        // Enough to recognise where a reconnect's audio catches up with what was received.
        static constexpr size_t SPLICE_KEY_BYTES = 4096;

        // A reconnect's repeat can only lie in the backlog it opens with, the part that arrives
        // faster than real time. Once its audio has kept to real time this long with no match,
        // nothing repeated. Long enough to ride out a pause inside the backlog, and to see a
        // backlog arriving at under twice real time still getting ahead.
        static constexpr std::chrono::milliseconds SPLICE_SETTLE { 1500 };

        // How often the speaker's connection is checked for the speaker hanging up, which it
        // does without a word: it sends nothing after its request.
        static constexpr std::chrono::milliseconds HANGUP_CHECK { 100 };

        // A play of the station the speaker left this recently carries on where it stopped.
        static constexpr std::chrono::seconds RESUME_WINDOW { 15 };

        // Anyone on the LAN can reach the relay; beyond this only the speaker is let in.
        static constexpr size_t MAX_CLIENTS = 8;

        // From the speaker opening its connection to first sound, measured with the backlog.
        static constexpr std::chrono::milliseconds SPEAKER_STARTUP { 350 };

        // With no icy-br header the rate is measured, skipping the CDN's opening burst.
        static constexpr std::chrono::seconds RATE_SETTLE { 4 };
        static constexpr std::chrono::seconds RATE_WINDOW { 6 };

        // The opening burst is over once the audio stops getting ahead of real time, give or
        // take ordinary jitter, for BURST_SETTLE. The CDN sends it in two parts with a pause of
        // up to ~340ms between them, then real time in 400ms chunks, so this has to sit between
        // the two. A connection older than BURST_LIMIT is past its burst.
        static constexpr std::chrono::milliseconds BURST_SETTLE { 450 };
        static constexpr double                    BURST_SLACK = 4096;
        static constexpr std::chrono::seconds      BURST_LIMIT { 5 };

        // Starting the speaker on a song needs this much of it already here. With less, it would
        // be starting nearly live (~2.6s to first sound against ~0.7s with a backlog), and would
        // trail the station by too little to tell a brief title from a song.
        static constexpr long MIN_START_LEAD_SECONDS = 3;
};

#endif
