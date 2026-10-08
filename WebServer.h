#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "HttpUtil.h"
#include "LiveSignal.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

// A small HTTP server for the dashboard and its JSON API. All it serves is a look at the configuration
// and the speaker, except the POSTs that work the speaker as its remote and buttons do: /api/volume
// sets its volume, /api/playback, /api/power, /api/preset and /api/skip press its Play, Pause,
// Power, preset and skip keys, /api/select presses the buttons that spell a preset, and /api/source
// switches it to Bluetooth or AUX. It owns a listening socket and serves each connection on a
// short-lived thread, in the manner of the stream relay.
//
// It is host-agnostic. The standalone "web" command runs it over the on-disk configuration and the
// speaker's own /nowPlaying; "control --web" runs it as a thread and supplies a live snapshot of
// what control is doing through a LiveStatus callback, so nothing has to query the speaker twice,
// and a LiveSignal it raises on each live change, so the dashboard's held /api/live/wait request
// answers the moment something such as the volume changes rather than at its next poll.
class WebServer
{
    public :

        // A snapshot of what control is playing now, or an empty object when nothing live is known.
        // Served at /api/live, and preferred for /api/nowplaying when present.
        using LiveStatus = std::function<nlohmann::json()>;

        struct Settings
        {
            int         port;
            std::string bind;               // dotted IPv4 to bind, or "" for every interface
            std::string version;
            long        relayPort;
            long        relayBufferMb;
            double      titleOffsetSeconds;
            bool        resume;
            bool        embedded;           // true when hosted inside control
            std::string defaultDeviceIp;    // the speaker the standalone server asks

            Settings ()
                : port (0),
                  relayPort (0),
                  relayBufferMb (0),
                  titleOffsetSeconds (0),
                  resume (true),
                  embedded (false)
            {
            }
        };

        // liveSignal, when given, must outlive the server; without it /api/live/wait is not served.
        WebServer (const Settings &settings, LiveStatus liveStatus = LiveStatus (), LiveSignal *liveSignal = nullptr);
        ~WebServer ();

        WebServer (const WebServer &) = delete;
        WebServer &operator= (const WebServer &) = delete;

        // Binds and starts serving; false when the port could not be taken.
        bool start ();
        void stop ();
        bool isRunning () const { return (m_running); }

    private :

        struct Connection
        {
            std::thread       thread;
            std::atomic<bool> done;

            Connection ()
                : done (false)
            {
            }
        };

        struct Response
        {
            int         status;
            std::string contentType;
            std::string body;
            std::string encoding;       // Content-Encoding, empty for none
        };

        void acceptLoop ();
        void serveConnection (Connection *connection, int fd);
        void reap (bool all);

        // Reads the body the head's Content-Length promises, from what came with the head and then
        // the socket. 0 once it is all there, else the status to refuse the request with.
        int readBody (int fd, const std::string &received, HttpRequest &request);

        Response route (const HttpRequest &request);

        nlohmann::json streamsJson () const;
        nlohmann::json devicesJson () const;
        nlohmann::json configJson () const;
        nlohmann::json stateJson () const;
        nlohmann::json nowPlayingJson ();
        nlohmann::json liveJson () const;

        // The live snapshot, held until it changes or LIVE_HOLD passes (see WebServer.cpp).
        Response liveWait (const HttpRequest &request);

        // POST /api/volume: sets the speaker's volume (see WebServer.cpp).
        Response setVolume (const HttpRequest &request);

        // POST /api/playback and /api/power: the remote's Play, Pause and Power keys (see WebServer.cpp).
        Response playback (const HttpRequest &request);
        Response power (const HttpRequest &request);

        // POST /api/preset, /api/skip and /api/source: the preset buttons, the remote's skip keys and the
        // speaker's source buttons (see WebServer.cpp).
        Response preset (const HttpRequest &request);
        Response skip (const HttpRequest &request);
        Response inputSource (const HttpRequest &request);

        // POST /api/select: a preset by its number, combos included, as `cxstcc select` (see WebServer.cpp).
        Response playPreset (const HttpRequest &request);

        // Presses and releases one of the remote's keys on the speaker, one key at a time.
        bool press (const std::string &key);

        // What the speaker is doing: its source (UPNP, STANDBY, ...) and play status. Both empty when
        // it is not known.
        struct SpeakerState
        {
            std::string source;
            std::string status;
        };

        // Inside control, control's view of it, kept by the speaker's events; standalone, a look at
        // the speaker.
        SpeakerState speakerState ();

        // Waits up to timeout for done to hold of the speaker's state: woken by each live change inside
        // control, polling the speaker standalone. True when it held.
        bool waitForSpeaker (const std::function<bool(const SpeakerState &)> &done, std::chrono::milliseconds timeout);

        // The speaker to ask: inside control, the one control drives; standalone, the default in
        // devices.json, read fresh.
        std::string speakerIp () const;

        // The display name and preset of whatever stream the location names: "" and 0 when none matches.
        void resolveStation (const std::string &location, std::string &name, int &preset) const;

        static Response json (int status, const nlohmann::json &body);
        static Response text (int status, const std::string &message);

        // Now the data members

        Settings                 m_settings;
        LiveStatus               m_liveStatus;
        LiveSignal              *m_liveSignal;
        std::atomic<int>         m_liveWaiters;
        int                      m_listenFd;
        std::atomic<bool>        m_running;
        std::atomic<bool>        m_stopping;
        std::thread              m_acceptThread;
        std::mutex               m_connectionsMutex;
        std::vector<std::unique_ptr<Connection>> m_connections;

        // A short cache so the dashboard's panels, which load together, do not each wake the speaker.
        mutable std::mutex       m_nowPlayingMutex;
        nlohmann::json           m_nowPlayingCache;
        bool                     m_nowPlayingCached;
        std::chrono::steady_clock::time_point m_nowPlayingAt;

        // One power change at a time, so that two clicks cannot undo each other.
        std::mutex               m_powerMutex;

        // One key, its press and its release, at a time, so that presses reach the speaker whole and
        // in the order taken: 1 then 1 must arrive as such to make preset 11.
        std::mutex               m_keyMutex;

        static constexpr size_t MAX_CONNECTIONS = 8;
        static constexpr std::chrono::milliseconds NOW_PLAYING_TTL { 1000 };

        // The largest request body taken; the only one there is, {"volume": N}, is a few bytes.
        static constexpr size_t MAX_BODY = 4096;

        // How much of a body too large to take is read and thrown away before refusing it.
        static constexpr size_t MAX_DISCARD = 64 * 1024;

        // How long setting the volume may take the speaker before the dashboard is told it failed.
        static constexpr long VOLUME_TIMEOUT_MS = 3000;

        // How long each of a key's press and release may take the speaker to answer.
        static constexpr long KEY_TIMEOUT_MS = 3000;

        // How long the speaker may take to show it has done what a key asked: to be playing (or
        // buffering) or paused, and to be on or in standby, before the dashboard is told it has not.
        static constexpr std::chrono::milliseconds PLAYBACK_SETTLE { 5000 };
        static constexpr std::chrono::milliseconds POWER_SETTLE { 8000 };

        // How long the speaker may take to be on Bluetooth or AUX once asked.
        static constexpr std::chrono::milliseconds SOURCE_SETTLE { 6000 };

        // A held request keeps its connection. This many leave room under MAX_CONNECTIONS for a page
        // loading at the same time, which asks for five things at once.
        static constexpr int MAX_LIVE_WAITERS = 3;

        // How long a held request waits for a change before answering anyway. The page asks again at
        // once, so a quiet speaker costs one request per hold, as the page's old 3 s poll did.
        static constexpr std::chrono::milliseconds LIVE_HOLD { 3000 };
};

#endif
