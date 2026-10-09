#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "HttpUtil.h"
#include "LiveSignal.h"
#include "DeviceDiscovery.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

// A small HTTP server for the dashboard and its JSON API. Most of what it serves is a look at the
// configuration and the speaker. The POSTs work the speaker as its remote and buttons do: /api/volume
// sets its volume, /api/playback, /api/power, /api/preset and /api/skip press its Play, Pause,
// Power, preset and skip keys, /api/select presses the buttons that spell a preset, and /api/source
// switches it to Bluetooth or AUX. /api/play plays a stream by its name, preset or not, and a PUT
// to /api/streams saves an edited stream list. It owns a listening socket and serves each connection
// on a short-lived thread, in the manner of the stream relay.
//
// A second page, /speakers, manages the speakers: while it is open it keeps a search of the network
// going (it renews a short lease with POST /api/speakers/discover) and shows what answers, and a PUT
// to /api/speakers/default makes one of them the default, saved in devices.json. It is named for
// speakers rather than devices so that groups of speakers, to play to together, can join it later.
//
// It is host-agnostic. The standalone "web" command runs it over the on-disk configuration and the
// speaker's own /nowPlaying; "control --web" runs it as a thread and supplies a live snapshot of
// what control is doing through a LiveStatus callback, so nothing has to query the speaker twice,
// and a LiveSignal it raises on each live change, so the dashboard's held /api/live/wait request
// answers the moment something such as the volume changes rather than at its next poll. Its
// ControlHooks let the dashboard play control's streams and hand it a saved stream list.
class WebServer
{
    public :

        // A snapshot of what control is playing now, or an empty object when nothing live is known.
        // Served at /api/live, and preferred for /api/nowplaying when present.
        using LiveStatus = std::function<nlohmann::json()>;

        // What control does for the dashboard, on the server's threads.
        struct ControlHooks
        {
            // Plays the stream of this name, as a press for it would: 0 once asked, else the HTTP
            // status to refuse with (404 for a stream control does not have).
            std::function<int(const std::string &)>  playStream;

            // Takes up the stream list just saved, given the text of the new streams.json.
            std::function<void(const std::string &)> streamsSaved;
        };

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
        WebServer (const Settings &settings, LiveStatus liveStatus = LiveStatus (), LiveSignal *liveSignal = nullptr,
                   ControlHooks hooks = ControlHooks ());
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
            std::vector<std::pair<std::string, std::string>> headers = {};     // any others, such as ETag
        };

        void acceptLoop ();
        void serveConnection (Connection *connection, int fd);
        void reap (bool all);

        // Reads the body the head's Content-Length promises, from what came with the head and then
        // the socket. 0 once it is all there, else the status to refuse the request with.
        int readBody (int fd, const std::string &received, HttpRequest &request);

        Response route (const HttpRequest &request);

        // GET /api/streams, with the ETag a save of it must send back (see WebServer.cpp).
        Response streamsList ();
        nlohmann::json devicesJson () const;

        // The Speakers page's API (see WebServer.cpp): GET /api/speakers, POST /api/speakers/discover,
        // which also keeps the search going, and PUT /api/speakers/default.
        nlohmann::json speakersJson ();
        Response discoverSpeakers (const HttpRequest &request);
        Response setDefaultSpeaker (const HttpRequest &request);

        // Keeps the search for speakers going for SEARCH_LEASE more, starting it when it is not running.
        void renewSearch ();

        // The search: a pass, a pause, and so on while its lease lasts.
        void searchLoop ();
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

        // POST /api/play and PUT /api/streams: a stream by its name, and the edited stream list
        // (see WebServer.cpp).
        Response playStream (const HttpRequest &request);
        Response saveStreams (const HttpRequest &request);

        // Presses and releases one of the remote's keys on the speaker, one key at a time.
        bool press (const std::string &key);

        // Presses the preset buttons that spell a preset, gapMs apart, with no other key between.
        bool pressButtons (const std::vector<int> &buttons, int gapMs);

        // streams.json as it is on disk: 0 with its text, ENOENT (and "") when there is none, else the
        // errno that stopped it being read.
        static int readStreamsFile (std::string &text);

        // What the speaker is doing: its source (UPNP, STANDBY, ...), play status and, for a stream,
        // the URL it is playing. All empty when it is not known.
        struct SpeakerState
        {
            std::string source;
            std::string status;
            std::string location;
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

        // The display name, preset and name of whatever stream the location names: "", 0 and "" when
        // none matches.
        void resolveStation (const std::string &location, std::string &station, int &preset, std::string &name) const;

        static Response json (int status, const nlohmann::json &body);
        static Response text (int status, const std::string &message);

        // Now the data members

        Settings                 m_settings;
        LiveStatus               m_liveStatus;
        LiveSignal              *m_liveSignal;
        ControlHooks             m_hooks;
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

        // One save of the stream list at a time, so each is checked against the file it replaces.
        std::mutex               m_streamsMutex;

        // One save of devices.json at a time.
        std::mutex               m_speakersMutex;

        // The search for speakers, and every speaker it has found by its device ID (or address, for
        // one that would not say its ID) with when it last answered. All under m_searchMutex.
        struct Sighting
        {
            SoundTouchDevice                      device;
            std::chrono::steady_clock::time_point at;
        };

        std::mutex               m_searchMutex;
        std::condition_variable  m_searchWake;
        std::thread              m_searchThread;
        bool                     m_searchRunning = false;
        bool                     m_searching = false;
        long                     m_searchPasses = 0;
        std::chrono::steady_clock::time_point m_searchUntil;
        std::chrono::steady_clock::time_point m_lastPassAt;
        std::map<std::string, Sighting> m_sightings;

        static constexpr size_t MAX_CONNECTIONS = 8;
        static constexpr std::chrono::milliseconds NOW_PLAYING_TTL { 1000 };

        // The largest request body taken: the remote's requests are a few bytes, such as {"volume": N}.
        static constexpr size_t MAX_BODY = 4096;

        // A whole stream list is larger: a stream is a few hundred bytes, so this is room for hundreds.
        static constexpr size_t MAX_STREAMS_BODY = 64 * 1024;

        // How much of a body too large to take is read and thrown away before refusing it.
        static constexpr size_t MAX_DISCARD = 128 * 1024;

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

        // How long control may take to have the speaker playing a stream asked for by name: it may
        // wait a few seconds for the station's first song title, and the speaker a few to start.
        static constexpr std::chrono::milliseconds PLAY_SETTLE { 15000 };

        // A held request keeps its connection. This many leave room under MAX_CONNECTIONS for a page
        // loading at the same time, which asks for five things at once.
        static constexpr int MAX_LIVE_WAITERS = 3;

        // How long a held request waits for a change before answering anyway. The page asks again at
        // once, so a quiet speaker costs one request per hold, as the page's old 3 s poll did.
        static constexpr std::chrono::milliseconds LIVE_HOLD { 3000 };

        // How long the search for speakers runs after the Speakers page last asked: the page asks every
        // couple of seconds while it is open, so the search stops soon after it closes.
        static constexpr std::chrono::milliseconds SEARCH_LEASE { 15000 };

        // How long each pass listens for answers, and the pause between passes.
        static constexpr int SEARCH_LISTEN_SECONDS = 2;
        static constexpr std::chrono::milliseconds SEARCH_PAUSE { 1000 };

        // A speaker is online when it answered within this, a few passes; one not saved that has not
        // answered for FORGET_UNSAVED is dropped from the list.
        static constexpr std::chrono::milliseconds ONLINE_WINDOW { 10000 };
        static constexpr std::chrono::milliseconds FORGET_UNSAVED { 120000 };
};

#endif
