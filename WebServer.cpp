#include "WebServer.h"
#include "WebJson.h"
#include "WebAssets.h"
#include "StreamConfig.h"
#include "DeviceDiscovery.h"
#include "SoundTouchClient.h"
#include "StreamProxy.h"
#include "Say.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace
{
    // The data files, read fresh on each request so edits made elsewhere show up without a restart.
    // They are relative because the program has already changed into the data directory.
    const char *STREAMS_FILE = "streams.json";
    const char *DEVICES_FILE = "devices.json";
    const char *STATE_FILE   = "state.json";

    const char *statusText (int status)
    {
        switch (status)
        {
            case 200 : return ("OK");
            case 400 : return ("Bad Request");
            case 404 : return ("Not Found");
            case 405 : return ("Method Not Allowed");
            case 409 : return ("Conflict");
            case 413 : return ("Payload Too Large");
            case 415 : return ("Unsupported Media Type");
            case 502 : return ("Bad Gateway");
            case 503 : return ("Service Unavailable");
            case 504 : return ("Gateway Timeout");
            default  : return ("OK");
        }
    }

    // The dashboard's changes come only as JSON, which a page on another site cannot send here without
    // a preflight this server never grants: so no other site can work the speaker through a browser.
    bool takesJson (const HttpRequest &request)
    {
        return (HttpUtil::mediaType (request.contentType) == "application/json");
    }

    bool sendAll (int fd, const char *data, size_t length)
    {
        while (length > 0)
        {
            const ssize_t sent = send (fd, data, length, MSG_NOSIGNAL);

            if (sent < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                return (false);
            }

            data += sent;
            length -= static_cast<size_t> (sent);
        }

        return (true);
    }

    std::string currentDirectory ()
    {
        char buffer[4096];

        if (getcwd (buffer, sizeof (buffer)) != nullptr)
        {
            return (std::string (buffer));
        }

        return (".");
    }
}

WebServer::WebServer (const Settings &settings, LiveStatus liveStatus, LiveSignal *liveSignal)
    : m_settings (settings),
      m_liveStatus (std::move (liveStatus)),
      m_liveSignal (liveSignal),
      m_liveWaiters (0),
      m_listenFd (-1),
      m_running (false),
      m_stopping (false),
      m_nowPlayingCached (false)
{
}

WebServer::~WebServer ()
{
    stop ();
}

bool WebServer::start ()
{
    m_listenFd = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (m_listenFd < 0)
    {
        Say (std::cerr) << "web: could not create socket\n";
        return (false);
    }

    const int one = 1;

    setsockopt (m_listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one));

    struct sockaddr_in addr;

    std::memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (static_cast<uint16_t> (m_settings.port));

    if (m_settings.bind.empty () || m_settings.bind == "0.0.0.0")
    {
        addr.sin_addr.s_addr = htonl (INADDR_ANY);
    }
    else if (inet_pton (AF_INET, m_settings.bind.c_str (), &addr.sin_addr) != 1)
    {
        Say (std::cerr) << "web: not a valid bind address: " << m_settings.bind << "\n";
        close (m_listenFd);
        m_listenFd = -1;
        return (false);
    }

    if (bind (m_listenFd, reinterpret_cast<struct sockaddr *> (&addr), sizeof (addr)) < 0)
    {
        Say (std::cerr) << "web: could not bind port " << m_settings.port << ": " << std::strerror (errno) << "\n";
        close (m_listenFd);
        m_listenFd = -1;
        return (false);
    }

    if (listen (m_listenFd, 16) < 0)
    {
        Say (std::cerr) << "web: listen failed: " << std::strerror (errno) << "\n";
        close (m_listenFd);
        m_listenFd = -1;
        return (false);
    }

    m_stopping = false;
    m_running = true;
    m_acceptThread = std::thread (&WebServer::acceptLoop, this);

    return (true);
}

void WebServer::stop ()
{
    if (!m_running.exchange (false))
    {
        return;
    }

    m_stopping = true;

    // A held /api/live/wait would otherwise keep its connection, and so this stop, for up to
    // LIVE_HOLD; woken, it sees m_stopping and answers at once.
    if (m_liveSignal != nullptr)
    {
        m_liveSignal->wakeAll ();
    }

    if (m_acceptThread.joinable ())
    {
        m_acceptThread.join ();
    }

    if (m_listenFd >= 0)
    {
        close (m_listenFd);
        m_listenFd = -1;
    }

    reap (true);
}

void WebServer::reap (bool all)
{
    std::lock_guard<std::mutex> lock (m_connectionsMutex);

    for (auto it = m_connections.begin (); it != m_connections.end (); )
    {
        if (all || (*it)->done)
        {
            if ((*it)->thread.joinable ())
            {
                (*it)->thread.join ();
            }

            it = m_connections.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

void WebServer::acceptLoop ()
{
    while (!m_stopping)
    {
        reap (false);

        struct pollfd pfd { m_listenFd, POLLIN, 0 };

        if (poll (&pfd, 1, 300) <= 0)
        {
            continue;
        }

        const int fd = accept (m_listenFd, nullptr, nullptr);

        if (fd < 0)
        {
            continue;
        }

        // A held request that has just answered is usually asked again at once; free its slot first
        // so the new one is not counted against it.
        reap (false);

        {
            std::lock_guard<std::mutex> lock (m_connectionsMutex);

            if (m_connections.size () >= MAX_CONNECTIONS)
            {
                // Too many at once: turn it away rather than queue unboundedly.
                static const char *busy = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n"
                                          "Content-Length: 0\r\n\r\n";

                sendAll (fd, busy, std::strlen (busy));
                close (fd);
                continue;
            }
        }

        struct timeval sendTimeout { 15, 0 };

        setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof (sendTimeout));

        auto connection = std::make_unique<Connection> ();
        Connection *raw = connection.get ();

        try
        {
            connection->thread = std::thread (&WebServer::serveConnection, this, raw, fd);
        }
        catch (const std::exception &error)
        {
            Say (std::cerr) << "web: could not serve a connection: " << error.what () << "\n";
            close (fd);
            continue;
        }

        std::lock_guard<std::mutex> lock (m_connectionsMutex);

        m_connections.push_back (std::move (connection));
    }
}

void WebServer::serveConnection (Connection *connection, int fd)
{
    std::string request;
    char scratch[2048];

    while (request.find ("\r\n\r\n") == std::string::npos && request.size () < 8192 && !m_stopping)
    {
        struct pollfd pfd { fd, POLLIN, 0 };

        if (poll (&pfd, 1, 5000) <= 0)
        {
            break;
        }

        const ssize_t got = recv (fd, scratch, sizeof (scratch), 0);

        if (got <= 0)
        {
            break;
        }

        request.append (scratch, static_cast<size_t> (got));
    }

    HttpRequest parsed;

    // One that arrives as the server stops is not read at all: it was not a bad request.
    Response response = m_stopping ? text (503, "Service Unavailable") : text (400, "Bad Request");

    if (HttpUtil::parseRequestLine (HttpUtil::firstLine (request), parsed))
    {
        const int refused = readBody (fd, request, parsed);

        response = (refused == 0)
                       ? route (parsed)
                       : json (refused, nlohmann::json { { "error", (refused == 413) ? "body too large" : "incomplete body" } });
    }

    const bool headOnly = (parsed.method == "HEAD");

    std::string head = "HTTP/1.1 " + std::to_string (response.status) + " " + statusText (response.status) + "\r\n";

    head += "Content-Type: " + response.contentType + "\r\n";

    if (!response.encoding.empty ())
    {
        head += "Content-Encoding: " + response.encoding + "\r\n";
    }

    head += "Content-Length: " + std::to_string (response.body.size ()) + "\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "Connection: close\r\n\r\n";

    if (sendAll (fd, head.data (), head.size ()) && !headOnly)
    {
        sendAll (fd, response.body.data (), response.body.size ());
    }

    close (fd);

    connection->done = true;
}

int WebServer::readBody (int fd, const std::string &received, HttpRequest &request)
{
    const size_t headEnd = received.find ("\r\n\r\n");

    // A head too long to have been read whole is still served, as it always was, only with no body.
    if (headEnd == std::string::npos)
    {
        return (0);
    }

    const std::string head = received.substr (0, headEnd);
    const std::string lengthText = HttpUtil::headerValue (head, "Content-Length");
    std::uint64_t length = 0;

    request.contentType = HttpUtil::headerValue (head, "Content-Type");

    if (lengthText.empty ())
    {
        return (0);
    }

    if (!HttpUtil::parseUnsigned (lengthText, length))
    {
        return (400);
    }

    const bool asksFirst = (HttpUtil::headerValue (head, "Expect") == "100-continue");

    // Too large, and the client is waiting to be told to send it: just refuse.
    if (length > MAX_BODY && asksFirst)
    {
        return (413);
    }

    std::string body = received.substr (headEnd + 4);

    // A body too large to take is still read, up to a point, and thrown away: closing with it unread
    // would reset the connection, and the client could lose the refusal.
    const std::uint64_t wanted = (length > MAX_BODY) ? std::min<std::uint64_t> (length, MAX_DISCARD) : length;

    // A client that asks before sending its body (curl does, for a larger one) is told to go ahead.
    if (body.size () < wanted && asksFirst)
    {
        static const char *goAhead = "HTTP/1.1 100 Continue\r\n\r\n";

        sendAll (fd, goAhead, std::strlen (goAhead));
    }

    char scratch[2048];

    while (body.size () < wanted && !m_stopping)
    {
        struct pollfd pfd { fd, POLLIN, 0 };

        if (poll (&pfd, 1, 5000) <= 0)
        {
            break;
        }

        const ssize_t got = recv (fd, scratch, sizeof (scratch), 0);

        if (got <= 0)
        {
            break;
        }

        body.append (scratch, static_cast<size_t> (got));
    }

    if (length > MAX_BODY)
    {
        return (413);
    }

    if (body.size () < length)
    {
        return (400);
    }

    body.resize (static_cast<size_t> (length));
    request.body = body;

    return (0);
}

WebServer::Response WebServer::route (const HttpRequest &request)
{
    const std::string &path = request.path;

    // The few things the dashboard changes, each a POST; everything else is only looked at.
    if (path == "/api/volume" || path == "/api/playback" || path == "/api/power")
    {
        if (request.method != "POST")
        {
            return (text (405, "Method Not Allowed"));
        }

        if (path == "/api/volume")
        {
            return (setVolume (request));
        }

        return ((path == "/api/playback") ? playback (request) : power (request));
    }

    if (request.method != "GET" && request.method != "HEAD")
    {
        return (text (405, "Method Not Allowed"));
    }

    if (path == "/" || path == "/index.html")
    {
        return (Response { 200, "text/html; charset=utf-8", DASHBOARD_HTML, "" });
    }

    if (path == "/api/health")
    {
        return (json (200, nlohmann::json { { "ok", true }, { "version", m_settings.version } }));
    }

    if (path == "/api/streams")
    {
        return (json (200, streamsJson ()));
    }

    if (path == "/api/devices")
    {
        return (json (200, devicesJson ()));
    }

    if (path == "/api/config")
    {
        return (json (200, configJson ()));
    }

    if (path == "/api/state")
    {
        return (json (200, stateJson ()));
    }

    if (path == "/api/nowplaying")
    {
        return (json (200, nowPlayingJson ()));
    }

    if (path == "/api/live")
    {
        return (json (200, liveJson ()));
    }

    if (path == "/api/live/wait" && m_liveSignal != nullptr)
    {
        return (liveWait (request));
    }

    return (json (404, nlohmann::json { { "error", "not found" }, { "path", path } }));
}

nlohmann::json WebServer::streamsJson () const
{
    StreamConfig config;

    config.loadFromFile (STREAMS_FILE, true);

    return (WebJson::streams (config.getStreams ()));
}

nlohmann::json WebServer::devicesJson () const
{
    DeviceDiscovery discovery;

    discovery.loadFromFile (DEVICES_FILE);

    const SoundTouchDevice *defaultDevice = discovery.getDefaultDevice ();
    const std::string defaultId = (defaultDevice != nullptr) ? defaultDevice->deviceId : std::string ();

    return (WebJson::devices (discovery.getDevices (), defaultId));
}

nlohmann::json WebServer::configJson () const
{
    nlohmann::json json;

    json["data_dir"] = currentDirectory ();
    json["web_port"] = m_settings.port;
    json["web_bind"] = m_settings.bind.empty () ? std::string ("0.0.0.0") : m_settings.bind;
    json["relay_port"] = m_settings.relayPort;
    json["relay_buffer_mb"] = m_settings.relayBufferMb;

    if (m_settings.relayBufferMb > 0)
    {
        json["pause_minutes"] = StreamProxy::pauseMinutes (
            static_cast<size_t> (m_settings.relayBufferMb) * 1024 * 1024, 8000);
    }
    else
    {
        json["pause_minutes"] = nullptr;
    }

    json["title_offset_seconds"] = m_settings.titleOffsetSeconds;
    json["resume"] = m_settings.resume;
    json["embedded"] = m_settings.embedded;
    json["version"] = m_settings.version;

    return (json);
}

nlohmann::json WebServer::stateJson () const
{
    nlohmann::json json;
    int lastPreset = 0;

    std::ifstream ifs (STATE_FILE);

    if (ifs.is_open ())
    {
        try
        {
            lastPreset = nlohmann::json::parse (ifs).value ("last_preset", 0);
        }
        catch (const std::exception &)
        {
            lastPreset = 0;
        }
    }

    json["last_preset"] = lastPreset;

    StreamConfig config;

    config.loadFromFile (STREAMS_FILE, true);

    const Stream *stream = (lastPreset != 0) ? config.findByPreset (lastPreset) : nullptr;

    json["station"] = (stream != nullptr) ? stream->displayName : std::string ();

    return (json);
}

std::string WebServer::resolveStation (const std::string &location) const
{
    if (location.empty ())
    {
        return (std::string ());
    }

    StreamConfig config;

    config.loadFromFile (STREAMS_FILE, true);

    if (const Stream *stream = config.findByUrl (location))
    {
        return (stream->displayName);
    }

    const std::string name = StreamProxy::streamNameFromUrl (location, 0);

    if (!name.empty ())
    {
        if (const Stream *stream = config.findByName (name))
        {
            return (stream->displayName);
        }
    }

    return (std::string ());
}

nlohmann::json WebServer::nowPlayingJson ()
{
    // Inside control the live snapshot is authoritative and needs no second trip to the speaker.
    if (m_liveStatus)
    {
        return (m_liveStatus ());
    }

    {
        std::lock_guard<std::mutex> lock (m_nowPlayingMutex);

        if (m_nowPlayingCached && std::chrono::steady_clock::now () - m_nowPlayingAt < NOW_PLAYING_TTL)
        {
            return (m_nowPlayingCache);
        }
    }

    SoundTouchClient client (speakerIp ());
    const SoundTouchClient::NowPlaying now = client.glance (1200);
    const SoundTouchClient::Volume vol = client.volume (1200);
    nlohmann::json json = WebJson::nowPlaying (now, resolveStation (now.location), vol);

    {
        std::lock_guard<std::mutex> lock (m_nowPlayingMutex);

        m_nowPlayingCache = json;
        m_nowPlayingCached = true;
        m_nowPlayingAt = std::chrono::steady_clock::now ();
    }

    return (json);
}

nlohmann::json WebServer::liveJson () const
{
    if (m_liveStatus)
    {
        return (m_liveStatus ());
    }

    return (nlohmann::json::object ());
}

// The live snapshot plus its "seq", the LiveSignal's count of changes. Given since, the seq of the
// snapshot the caller already has, it holds the answer until something changes, which wakes it at
// once, or LIVE_HOLD passes. Without since, or behind, it answers at once. The page asks again as
// each answer arrives, so a change such as the volume reaches it in milliseconds, not at a poll.
WebServer::Response WebServer::liveWait (const HttpRequest &request)
{
    std::uint64_t seq = m_liveSignal->seq ();
    std::uint64_t since = 0;
    const bool upToDate = HttpUtil::parseUnsigned (HttpUtil::queryValue (request.query, "since"), since)
                          && since == seq;

    if (upToDate && request.method == "GET")
    {
        // Past this many held at once the caller is told to poll for now: each keeps a connection.
        if (m_liveWaiters.fetch_add (1) >= MAX_LIVE_WAITERS)
        {
            --m_liveWaiters;

            return (json (503, nlohmann::json { { "error", "too many waiting" } }));
        }

        seq = m_liveSignal->waitPast (since, LIVE_HOLD, m_stopping);

        --m_liveWaiters;
    }

    nlohmann::json body = m_liveStatus ? m_liveStatus () : nlohmann::json::object ();

    body["seq"] = seq;

    return (json (200, body));
}

// Sets the speaker's volume: POST {"volume": N}, N from 0 to 100, as JSON only (see takesJson). Inside
// control the speaker's own report of the new level then reaches every open dashboard through
// /api/live/wait.
WebServer::Response WebServer::setVolume (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"volume\": 0..100}" } }));
    }

    int level = 0;

    if (!WebJson::parseVolume (request.body, level))
    {
        return (json (400, nlohmann::json { { "error", "expected {\"volume\": 0..100}" } }));
    }

    SoundTouchClient client (speakerIp ());

    if (!client.setVolume (level, VOLUME_TIMEOUT_MS))
    {
        return (json (502, nlohmann::json { { "error", "the speaker did not take it" } }));
    }

    // The standalone server's cached look at the speaker is out of date now.
    {
        std::lock_guard<std::mutex> lock (m_nowPlayingMutex);

        m_nowPlayingCached = false;
    }

    return (json (200, nlohmann::json { { "ok", true }, { "volume", level } }));
}

// The remote's Play and Pause: POST {"action": "play"} or {"action": "pause"}. Explicit keys rather
// than PLAY_PAUSE, so a second click cannot flip it back. The speaker sends the same events as for the
// remote, so control carries a pause through its relay just the same. Answers once the speaker is
// playing (or buffering) or paused: 409 when it is off, 504 when it has not done it in time.
WebServer::Response WebServer::playback (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"action\": \"play\" or \"pause\"}" } }));
    }

    std::string action;

    if (!WebJson::parsePlayback (request.body, action))
    {
        return (json (400, nlohmann::json { { "error", "expected {\"action\": \"play\" or \"pause\"}" } }));
    }

    if (speakerState ().source == "STANDBY")
    {
        return (json (409, nlohmann::json { { "error", "the speaker is off" } }));
    }

    const bool play = (action == "play");
    SoundTouchClient client (speakerIp ());

    if (!client.pressKey (play ? "PLAY" : "PAUSE", KEY_TIMEOUT_MS))
    {
        return (json (502, nlohmann::json { { "error", "the speaker did not take the key" } }));
    }

    const bool done = waitForSpeaker ([play] (const SpeakerState &state)
    {
        return (play ? (state.status == "PLAY_STATE" || state.status == "BUFFERING_STATE")
                     : (state.status == "PAUSE_STATE" || state.status == "STOP_STATE"));
    }, PLAYBACK_SETTLE);

    {
        std::lock_guard<std::mutex> lock (m_nowPlayingMutex);

        m_nowPlayingCached = false;
    }

    if (!done)
    {
        return (json (504, nlohmann::json { { "error", play ? "the speaker did not start playing" : "the speaker did not pause" } }));
    }

    return (json (200, nlohmann::json { { "ok", true }, { "action", action } }));
}

// The remote's Power key, made to say which way: POST {"on": true} or {"on": false} presses it only
// when the speaker is not already that way, one change at a time, and answers once it has switched
// (504 when it has not in time). Off is standby, which control takes as meant, as from the remote.
WebServer::Response WebServer::power (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"on\": true or false}" } }));
    }

    bool on = false;

    if (!WebJson::parsePower (request.body, on))
    {
        return (json (400, nlohmann::json { { "error", "expected {\"on\": true or false}" } }));
    }

    std::lock_guard<std::mutex> lock (m_powerMutex);

    const SpeakerState before = speakerState ();

    if (before.source.empty ())
    {
        return (json (502, nlohmann::json { { "error", "the speaker did not say whether it is on" } }));
    }

    if ((before.source != "STANDBY") == on)
    {
        return (json (200, nlohmann::json { { "ok", true }, { "on", on }, { "changed", false } }));
    }

    SoundTouchClient client (speakerIp ());

    if (!client.pressKey ("POWER", KEY_TIMEOUT_MS))
    {
        return (json (502, nlohmann::json { { "error", "the speaker did not take the key" } }));
    }

    const bool done = waitForSpeaker ([on] (const SpeakerState &state)
    {
        return (!state.source.empty () && (state.source != "STANDBY") == on);
    }, POWER_SETTLE);

    {
        std::lock_guard<std::mutex> cacheLock (m_nowPlayingMutex);

        m_nowPlayingCached = false;
    }

    if (!done)
    {
        return (json (504, nlohmann::json { { "error", on ? "the speaker did not switch on" : "the speaker did not switch off" } }));
    }

    return (json (200, nlohmann::json { { "ok", true }, { "on", on }, { "changed", true } }));
}

WebServer::SpeakerState WebServer::speakerState ()
{
    SpeakerState state;

    if (m_liveStatus)
    {
        const nlohmann::json live = m_liveStatus ();
        const auto field = [&live] (const char *key)
        {
            const auto found = live.find (key);

            return ((found != live.end () && found->is_string ()) ? found->get<std::string> () : std::string ());
        };

        state.source = field ("source");
        state.status = field ("status");

        return (state);
    }

    SoundTouchClient client (speakerIp ());
    const SoundTouchClient::NowPlaying now = client.glance (1200);

    state.source = now.source;
    state.status = now.status;

    return (state);
}

bool WebServer::waitForSpeaker (const std::function<bool(const SpeakerState &)> &done, std::chrono::milliseconds timeout)
{
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now () + timeout;

    while (!m_stopping)
    {
        // The sequence is read before the state, so a change in between still ends the wait below.
        const std::uint64_t seq = (m_liveSignal != nullptr) ? m_liveSignal->seq () : 0;

        if (done (speakerState ()))
        {
            return (true);
        }

        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ();

        if (now >= deadline)
        {
            return (false);
        }

        const std::chrono::milliseconds left = std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now);

        if (m_liveSignal != nullptr)
        {
            m_liveSignal->waitPast (seq, left, m_stopping);
        }
        else
        {
            std::this_thread::sleep_for (std::min (left, std::chrono::milliseconds (250)));
        }
    }

    return (false);
}

std::string WebServer::speakerIp () const
{
    if (m_settings.embedded && !m_settings.defaultDeviceIp.empty ())
    {
        return (m_settings.defaultDeviceIp);
    }

    std::string deviceIp = m_settings.defaultDeviceIp.empty () ? std::string ("192.168.3.53") : m_settings.defaultDeviceIp;
    DeviceDiscovery discovery;

    if (discovery.loadFromFile (DEVICES_FILE))
    {
        if (const SoundTouchDevice *device = discovery.getDefaultDevice ())
        {
            deviceIp = device->ipAddress;
        }
    }

    return (deviceIp);
}

WebServer::Response WebServer::json (int status, const nlohmann::json &body)
{
    return (Response { status, "application/json; charset=utf-8", body.dump (), "" });
}

WebServer::Response WebServer::text (int status, const std::string &message)
{
    return (Response { status, "text/plain; charset=utf-8", message + "\n", "" });
}
