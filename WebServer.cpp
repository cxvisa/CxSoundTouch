#include "WebServer.h"
#include "WebJson.h"
#include "WebAssets.h"
#include "StreamConfig.h"
#include "DeviceDiscovery.h"
#include "SpeakerConfig.h"
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
#include <iterator>

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
            case 412 : return ("Precondition Failed");
            case 413 : return ("Payload Too Large");
            case 415 : return ("Unsupported Media Type");
            case 428 : return ("Precondition Required");
            case 500 : return ("Internal Server Error");
            case 502 : return ("Bad Gateway");
            case 503 : return ("Service Unavailable");
            case 504 : return ("Gateway Timeout");
            default  : return ("OK");
        }
    }

    // A data file as it is on disk: 0 with its text, ENOENT (and "") when there is none, else the errno
    // that stopped it being read.
    int readDataFile (const char *path, std::string &text)
    {
        text.clear ();

        errno = 0;

        std::ifstream ifs (path, std::ios::binary);

        if (!ifs.is_open ())
        {
            return ((errno != 0) ? errno : ENOENT);
        }

        try
        {
            text.assign (std::istreambuf_iterator<char> (ifs), std::istreambuf_iterator<char> ());
        }
        catch (const std::exception &)
        {
            // Such as a directory where the file should be.
            text.clear ();
            return ((errno != 0) ? errno : EIO);
        }

        return (0);
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

WebServer::WebServer (const Settings &settings, LiveStatus liveStatus, LiveSignal *liveSignal, ControlHooks hooks)
    : m_settings (settings),
      m_liveStatus (std::move (liveStatus)),
      m_liveSignal (liveSignal),
      m_hooks (std::move (hooks)),
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

    // The search for speakers ends at once: woken in its pause, or cancelled within ~100 ms in a pass.
    {
        std::lock_guard<std::mutex> lock (m_searchMutex);
        m_searchWake.notify_all ();
    }

    if (m_searchThread.joinable ())
    {
        m_searchThread.join ();
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

    for (const auto &[name, value] : response.headers)
    {
        head += name + ": " + value + "\r\n";
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
    request.ifMatch = HttpUtil::headerValue (head, "If-Match");

    if (lengthText.empty ())
    {
        return (0);
    }

    if (!HttpUtil::parseUnsigned (lengthText, length))
    {
        return (400);
    }

    const bool asksFirst = (HttpUtil::headerValue (head, "Expect") == "100-continue");
    const size_t limit = (request.path == "/api/streams") ? MAX_STREAMS_BODY : MAX_BODY;

    // Too large, and the client is waiting to be told to send it: just refuse.
    if (length > limit && asksFirst)
    {
        return (413);
    }

    std::string body = received.substr (headEnd + 4);

    // A body too large to take is still read, up to a point, and thrown away: closing with it unread
    // would reset the connection, and the client could lose the refusal.
    const std::uint64_t wanted = (length > limit) ? std::min<std::uint64_t> (length, MAX_DISCARD) : length;

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

    if (length > limit)
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

    // The things the dashboard changes, each a POST, and the stream list it saves with a PUT;
    // everything else is only looked at.
    if (path == "/api/volume" || path == "/api/playback" || path == "/api/power" || path == "/api/preset"
        || path == "/api/skip" || path == "/api/source" || path == "/api/select" || path == "/api/play")
    {
        if (request.method != "POST")
        {
            return (text (405, "Method Not Allowed"));
        }

        if (path == "/api/volume")
        {
            return (setVolume (request));
        }

        if (path == "/api/playback")
        {
            return (playback (request));
        }

        if (path == "/api/power")
        {
            return (power (request));
        }

        if (path == "/api/preset")
        {
            return (preset (request));
        }

        if (path == "/api/select")
        {
            return (playPreset (request));
        }

        if (path == "/api/play")
        {
            return (playStream (request));
        }

        return ((path == "/api/skip") ? skip (request) : inputSource (request));
    }

    if (path == "/api/streams" && request.method == "PUT")
    {
        return (saveStreams (request));
    }

    if (path == "/api/speakers/discover")
    {
        return ((request.method == "POST") ? discoverSpeakers (request) : text (405, "Method Not Allowed"));
    }

    if (path == "/api/speakers/default")
    {
        return ((request.method == "PUT") ? setDefaultSpeaker (request) : text (405, "Method Not Allowed"));
    }

    if (request.method != "GET" && request.method != "HEAD")
    {
        return (text (405, "Method Not Allowed"));
    }

    if (path == "/" || path == "/index.html")
    {
        return (Response { 200, "text/html; charset=utf-8", DASHBOARD_HTML, "" });
    }

    if (path == "/speakers")
    {
        return (Response { 200, "text/html; charset=utf-8", SPEAKERS_HTML, "" });
    }

    if (path == "/api/speakers")
    {
        return (json (200, speakersJson ()));
    }

    if (path == "/api/health")
    {
        return (json (200, nlohmann::json { { "ok", true }, { "version", m_settings.version } }));
    }

    if (path == "/api/streams")
    {
        return (streamsList ());
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

// The stream list as streams.json has it, read fresh, with its ETag: the version of the file it came
// from, which a save of the list must send back as If-Match (see saveStreams).
WebServer::Response WebServer::streamsList ()
{
    std::string text;
    const int failure = readStreamsFile (text);
    StreamConfig config;

    if (failure == 0)
    {
        config.loadFromText (text, true, STREAMS_FILE);
    }
    else if (failure != ENOENT)
    {
        Say (std::cerr) << "Error: cannot read " << STREAMS_FILE << ": " << std::strerror (failure) << "\n";
    }

    Response response = json (200, WebJson::streams (config.getStreams ()));

    // A file that cannot be read gets no ETag, so nothing can be saved over it unseen.
    if (failure == 0 || failure == ENOENT)
    {
        response.headers.emplace_back ("ETag", HttpUtil::entityTag (text));
    }

    return (response);
}

int WebServer::readStreamsFile (std::string &text)
{
    return (readDataFile (STREAMS_FILE, text));
}

nlohmann::json WebServer::devicesJson () const
{
    DeviceDiscovery discovery;

    discovery.loadFromFile (DEVICES_FILE);

    const SoundTouchDevice *defaultDevice = discovery.getDefaultDevice ();
    const std::string defaultId = (defaultDevice != nullptr) ? defaultDevice->deviceId : std::string ();

    return (WebJson::devices (discovery.getDevices (), defaultId));
}

// The saved speakers and those the search has found, for the Speakers page (see WebJson::speakers).
nlohmann::json WebServer::speakersJson ()
{
    std::string text;
    SpeakerConfig saved;

    if (readDataFile (DEVICES_FILE, text) == 0)
    {
        saved.loadFromText (text);
    }

    std::vector<WebJson::SeenSpeaker> seen;
    WebJson::SpeakerSearch search;

    {
        std::lock_guard<std::mutex> lock (m_searchMutex);
        const auto now = std::chrono::steady_clock::now ();

        for (const auto &[key, sighting] : m_sightings)
        {
            WebJson::SeenSpeaker item;

            item.device = sighting.device;
            item.agoMs = std::chrono::duration_cast<std::chrono::milliseconds> (now - sighting.at).count ();
            seen.push_back (item);
        }

        search.active = m_searchRunning;
        search.searching = m_searching;
        search.passes = m_searchPasses;

        if (m_searchPasses > 0)
        {
            search.lastAgoMs = std::chrono::duration_cast<std::chrono::milliseconds> (now - m_lastPassAt).count ();
        }
    }

    return (WebJson::speakers (saved, seen, search, ONLINE_WINDOW.count (),
                               m_settings.embedded ? m_settings.defaultDeviceIp : std::string ()));
}

// POST /api/speakers/discover, as JSON (see takesJson): keeps the search for speakers going for
// SEARCH_LEASE more, starting it if need be, and answers what is known so far. The Speakers page asks
// every couple of seconds while it is open; nothing searches once no page has asked for a while.
WebServer::Response WebServer::discoverSpeakers (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {}" } }));
    }

    renewSearch ();

    return (json (200, speakersJson ()));
}

void WebServer::renewSearch ()
{
    std::lock_guard<std::mutex> lock (m_searchMutex);

    m_searchUntil = std::chrono::steady_clock::now () + SEARCH_LEASE;

    if (m_searchRunning || m_stopping)
    {
        return;
    }

    // A search that ran out has already finished with the mutex, so joining it here cannot wait on it.
    if (m_searchThread.joinable ())
    {
        m_searchThread.join ();
    }

    m_searchRunning = true;
    m_searchThread = std::thread (&WebServer::searchLoop, this);
}

void WebServer::searchLoop ()
{
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock (m_searchMutex);

            if (m_stopping || std::chrono::steady_clock::now () >= m_searchUntil)
            {
                m_searchRunning = false;
                m_searching = false;
                return;
            }

            m_searching = true;
        }

        DeviceDiscovery discovery;

        discovery.discover (SEARCH_LISTEN_SECONDS, true, &m_stopping);

        {
            std::lock_guard<std::mutex> lock (m_searchMutex);
            const auto now = std::chrono::steady_clock::now ();

            for (const SoundTouchDevice &device : discovery.getDevices ())
            {
                // Only what says who it is on :8090/info, as a SoundTouch does: other media players
                // answer the search too. A speaker already known that did not say this time is still
                // there, at the address it had.
                if (device.deviceId.empty ())
                {
                    for (auto &entry : m_sightings)
                    {
                        if (entry.second.device.ipAddress == device.ipAddress)
                        {
                            entry.second.at = now;
                        }
                    }

                    continue;
                }

                m_sightings[device.deviceId] = Sighting { device, now };
            }

            for (auto it = m_sightings.begin (); it != m_sightings.end (); )
            {
                it = (now - it->second.at > FORGET_UNSAVED) ? m_sightings.erase (it) : std::next (it);
            }

            m_searching = false;
            ++m_searchPasses;
            m_lastPassAt = now;
        }

        std::unique_lock<std::mutex> lock (m_searchMutex);

        m_searchWake.wait_for (lock, SEARCH_PAUSE, [this] () { return (m_stopping.load ()); });
    }
}

// PUT /api/speakers/default {"id": "..."}, as JSON: makes the speaker of that device ID the default,
// saving devices.json. A speaker the search found is saved with it; a saved one the search found at a
// new address is saved at that address. Standalone, the dashboard drives the new default at once;
// inside control it is taken up when control next starts, which the answer's restart_needed says.
WebServer::Response WebServer::setDefaultSpeaker (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"id\": \"<device id>\"}" } }));
    }

    std::string id;

    if (!WebJson::parseSpeakerId (request.body, id))
    {
        return (json (400, nlohmann::json { { "error", "send {\"id\": \"<device id>\"}" } }));
    }

    std::vector<SoundTouchDevice> found;

    {
        std::lock_guard<std::mutex> lock (m_searchMutex);

        for (const auto &[key, sighting] : m_sightings)
        {
            if (!sighting.device.deviceId.empty ())
            {
                found.push_back (sighting.device);
            }
        }
    }

    std::lock_guard<std::mutex> lock (m_speakersMutex);

    std::string existing;
    const int failure = readDataFile (DEVICES_FILE, existing);

    if (failure != 0 && failure != ENOENT)
    {
        return (json (500, nlohmann::json { { "error", std::string ("cannot read ") + DEVICES_FILE + ": " + std::strerror (failure) } }));
    }

    SpeakerConfig speakers;

    // A file that cannot be read is not written over, so nothing in it is lost.
    if (!existing.empty () && !speakers.loadFromText (existing))
    {
        return (json (409, nlohmann::json { { "error", std::string (DEVICES_FILE) + " is not valid JSON; fix or remove it first" } }));
    }

    for (const SoundTouchDevice &device : found)
    {
        if (device.deviceId == id || speakers.findById (device.deviceId) != nullptr)
        {
            speakers.remember (device);
        }
    }

    if (!speakers.setDefault (id))
    {
        return (json (404, nlohmann::json { { "error", "no speaker with that ID has been found or saved" }, { "id", id } }));
    }

    const std::string text = speakers.fileText (existing);
    const bool changed = (text != existing);

    if (changed)
    {
        std::string error;

        if (!StreamConfig::saveFile (DEVICES_FILE, text, existing, error))
        {
            return (json (500, nlohmann::json { { "error", error } }));
        }

        const SoundTouchDevice *chosen = speakers.findById (id);

        Say () << ">>> Default speaker set from the dashboard: " << id
               << (chosen->deviceName.empty () ? std::string () : " (" + chosen->deviceName + ")")
               << " at " << chosen->ipAddress << "\n";
    }

    const SoundTouchDevice *chosen = speakers.findById (id);
    nlohmann::json answer = speakersJson ();

    answer["ok"] = true;
    answer["changed"] = changed;
    answer["restart_needed"] = m_settings.embedded && chosen->ipAddress != m_settings.defaultDeviceIp;

    return (json (200, answer));
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

    // How long control waits for another digit of a combo, which the page's keypad waits at least.
    StreamConfig config;

    config.loadFromFile (STREAMS_FILE, true);
    json["combo_window_ms"] = config.getComboWindowMs ();

    return (json);
}

nlohmann::json WebServer::stateJson () const
{
    nlohmann::json json;
    int lastPreset = 0;
    std::string lastStream;

    std::ifstream ifs (STATE_FILE);

    if (ifs.is_open ())
    {
        try
        {
            const nlohmann::json state = nlohmann::json::parse (ifs);

            lastPreset = state.value ("last_preset", 0);
            lastStream = state.value ("last_stream", std::string ());
        }
        catch (const std::exception &)
        {
            lastPreset = 0;
            lastStream.clear ();
        }
    }

    json["last_preset"] = lastPreset;
    json["last_stream"] = lastStream;

    StreamConfig config;

    config.loadFromFile (STREAMS_FILE, true);

    // What played last is known by its name; a state.json from before that, only by its preset.
    const Stream *stream = !lastStream.empty () ? config.findByName (lastStream)
                         : (lastPreset != 0) ? config.findByPreset (lastPreset) : nullptr;

    json["station"] = (stream != nullptr) ? stream->displayName : std::string ();

    return (json);
}

void WebServer::resolveStation (const std::string &location, std::string &station, int &preset, std::string &name) const
{
    station.clear ();
    preset = 0;
    name.clear ();

    if (location.empty ())
    {
        return;
    }

    StreamConfig config;

    config.loadFromFile (STREAMS_FILE, true);

    const Stream *stream = config.findByUrl (location);

    if (stream == nullptr)
    {
        const std::string streamName = StreamProxy::streamNameFromUrl (location, 0);

        if (!streamName.empty ())
        {
            stream = config.findByName (streamName);
        }
    }

    if (stream != nullptr)
    {
        station = stream->displayName;
        preset = stream->preset;
        name = stream->name;
    }
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
    std::string station;
    int stationPreset = 0;
    std::string stationName;

    resolveStation (now.location, station, stationPreset, stationName);

    nlohmann::json json = WebJson::nowPlaying (now, station, vol, stationPreset, stationName);

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

    if (!press (play ? "PLAY" : "PAUSE"))
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

    if (!press ("POWER"))
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

// A preset button, as on the speaker and the remote: POST {"preset": N}, N from 1 to 6, presses
// PRESET_N. The speaker reports the press and control plays the station mapped to it, from standby
// too; a second press soon after makes a combo (1 then 1 is preset 11), as on the remote, which is why
// the page sends its presses one after another. Answers once the speaker has taken the key.
WebServer::Response WebServer::preset (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"preset\": 1..6}" } }));
    }

    int number = 0;

    if (!WebJson::parsePreset (request.body, number))
    {
        return (json (400, nlohmann::json { { "error", "expected {\"preset\": 1..6}" } }));
    }

    if (!press ("PRESET_" + std::to_string (number)))
    {
        return (json (502, nlohmann::json { { "error", "the speaker did not take the key" } }));
    }

    return (json (200, nlohmann::json { { "ok", true }, { "preset", number } }));
}

// A preset by its number, as `cxstcc select` does it: POST {"preset": N} presses the buttons that
// spell N (111 is 1, 1, 1) a third of the combo window apart, as select does, so control takes them as
// the remote's combo however fast or slowly they were typed or clicked. Refused, pressing nothing,
// when N is not made of the buttons 1 to 6 (400) or has no station on it (404), unlike the remote,
// whose unmapped press silences the speaker. The buttons are pressed under the key lock throughout,
// so no other press can land between them.
WebServer::Response WebServer::playPreset (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"preset\": N}" } }));
    }

    int number = 0;

    if (!WebJson::parseSelect (request.body, number))
    {
        return (json (400, nlohmann::json { { "error", "expected {\"preset\": N}" } }));
    }

    std::vector<int> buttons;

    if (!WebJson::buttonsOf (number, buttons))
    {
        return (json (400, nlohmann::json { { "error", "presets are made of the buttons 1 to 6, up to three of them: "
                                                       + std::to_string (number) + " is not one" } }));
    }

    StreamConfig config;

    config.loadFromFile (STREAMS_FILE, true);

    const Stream *stream = config.findByPreset (number);

    if (stream == nullptr)
    {
        return (json (404, nlohmann::json { { "error", "nothing on preset " + std::to_string (number) } }));
    }

    const std::string station = stream->displayName.empty () ? stream->name : stream->displayName;
    const int gapMs = std::max (50, config.getComboWindowMs () / 3);

    if (!pressButtons (buttons, gapMs))
    {
        return (json (502, nlohmann::json { { "error", "the speaker did not take the key" } }));
    }

    return (json (200, nlohmann::json { { "ok", true }, { "preset", number }, { "station", station } }));
}

bool WebServer::pressButtons (const std::vector<int> &buttons, int gapMs)
{
    std::lock_guard<std::mutex> lock (m_keyMutex);
    SoundTouchClient client (speakerIp ());

    for (size_t i = 0; i < buttons.size (); ++i)
    {
        if (i != 0)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (gapMs));
        }

        if (!client.pressKey ("PRESET_" + std::to_string (buttons[i]), KEY_TIMEOUT_MS))
        {
            return (false);
        }
    }

    return (true);
}

// A stream by its name, as the play button beside it on the dashboard: POST {"stream": "klove"}, on a
// preset or not. Inside control, control plays it just as it plays a preset when one is pressed: the
// press stops what was playing, and the stream starts through the relay, with its song titles, and is
// what control brings back after a drop or a restart. It answers once the speaker is playing it, 504
// when it is not in time. Standalone, a stream on a preset has its buttons pressed, as /api/select
// does, for control to play wherever it runs; one on no preset is handed to the speaker directly, as
// `cxstcc play` does. 404 for a name no stream has.
WebServer::Response WebServer::playStream (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"stream\": name}" } }));
    }

    std::string name;

    if (!WebJson::parsePlayStream (request.body, name))
    {
        return (json (400, nlohmann::json { { "error", "expected {\"stream\": name}, a stream's name" } }));
    }

    StreamConfig config;
    std::string fileText;

    if (readStreamsFile (fileText) == 0)
    {
        config.loadFromText (fileText, true, STREAMS_FILE);
    }

    const Stream *found = config.findByName (name);

    if (found == nullptr)
    {
        return (json (404, nlohmann::json { { "error", "no stream is called " + name } }));
    }

    const Stream stream = *found;
    const std::string station = stream.displayName.empty () ? stream.name : stream.displayName;
    nlohmann::json answer { { "ok", true }, { "stream", name }, { "station", station } };

    if (m_hooks.playStream)
    {
        const int refused = m_hooks.playStream (name);

        if (refused != 0)
        {
            return (json (refused, nlohmann::json { { "error", (refused == 404)
                                                          ? "control has no stream called " + name + "; save the list, or restart control"
                                                          : std::string ("control cannot play it now") } }));
        }

        // Through the relay the speaker is given the relay's address, with the stream's name in it.
        const bool started = waitForSpeaker ([&stream] (const SpeakerState &state)
        {
            return (state.source == "UPNP" && (state.status == "PLAY_STATE" || state.status == "BUFFERING_STATE")
                    && (state.location == stream.url || StreamProxy::streamNameFromUrl (state.location, 0) == stream.name));
        }, PLAY_SETTLE);

        if (!started)
        {
            return (json (504, nlohmann::json { { "error", station + " has not started" } }));
        }

        return (json (200, answer));
    }

    std::vector<int> buttons;

    if (stream.preset > 0 && WebJson::buttonsOf (stream.preset, buttons))
    {
        if (!pressButtons (buttons, std::max (50, config.getComboWindowMs () / 3)))
        {
            return (json (502, nlohmann::json { { "error", "the speaker did not take the key" } }));
        }

        answer["preset"] = stream.preset;

        return (json (200, answer));
    }

    SoundTouchClient client (speakerIp ());
    const bool played = client.playStream (stream.url, station);

    {
        std::lock_guard<std::mutex> lock (m_nowPlayingMutex);

        m_nowPlayingCached = false;
    }

    if (!played)
    {
        return (json (502, nlohmann::json { { "error", "the speaker did not play " + station } }));
    }

    return (json (200, answer));
}

// The whole stream list, as the dashboard's editor saves it: PUT {"streams": [...]}, the streams in
// the order wanted, with If-Match set to the ETag the list was read with (GET /api/streams). A list
// changed meanwhile, from another dashboard or by hand, is not overwritten unseen: that is 412, and
// no If-Match at all is 428. A list that breaks the rules is 400, saying what is wrong with which
// stream, and nothing is written. Otherwise streams.json is rewritten whole, keeping combo_window_ms
// and whatever else it holds, with the old one kept as streams.json.bak. Inside control the new list
// takes effect at once: a stream starts as it now is the next time it is played.
WebServer::Response WebServer::saveStreams (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"streams\": [...]}" } }));
    }

    if (request.ifMatch.empty ())
    {
        return (json (428, nlohmann::json { { "error", "send If-Match with the ETag of the list being changed, from GET /api/streams" } }));
    }

    std::vector<Stream> streams;
    nlohmann::json problems;

    if (!WebJson::parseStreams (request.body, streams, problems))
    {
        const std::string first = problems.empty () ? std::string ("not a stream list")
                                                     : problems[0].value ("error", std::string ("not a stream list"));

        return (json (400, nlohmann::json { { "error", first }, { "problems", problems } }));
    }

    std::lock_guard<std::mutex> lock (m_streamsMutex);

    std::string existing;
    const int failure = readStreamsFile (existing);

    if (failure != 0 && failure != ENOENT)
    {
        return (json (500, nlohmann::json { { "error", std::string ("cannot read ") + STREAMS_FILE + ": " + std::strerror (failure) } }));
    }

    const std::string current = HttpUtil::entityTag (existing);

    if (!HttpUtil::ifMatches (request.ifMatch, current))
    {
        Response refused = json (412, nlohmann::json { { "error", "the streams have been changed since this list was loaded" } });

        refused.headers.emplace_back ("ETag", current);

        return (refused);
    }

    const std::string text = StreamConfig::fileText (streams, existing);
    const bool changed = (text != existing);

    if (changed)
    {
        std::string error;

        if (!StreamConfig::saveFile (STREAMS_FILE, text, existing, error))
        {
            return (json (500, nlohmann::json { { "error", error } }));
        }

        Say () << ">>> " << STREAMS_FILE << " saved from the dashboard: " << streams.size ()
               << (streams.size () == 1 ? " stream\n" : " streams\n");

        if (m_hooks.streamsSaved)
        {
            m_hooks.streamsSaved (text);
        }
    }

    StreamConfig saved;

    saved.loadFromText (text, true, STREAMS_FILE);

    Response response = json (200, nlohmann::json { { "ok", true }, { "changed", changed },
                                                    { "applied", changed && static_cast<bool> (m_hooks.streamsSaved) },
                                                    { "streams", WebJson::streams (saved.getStreams ()) } });

    response.headers.emplace_back ("ETag", HttpUtil::entityTag (text));

    return (response);
}

// The remote's skip keys: POST {"direction": "next"} or {"direction": "previous"} presses NEXT_TRACK
// or PREV_TRACK. On one of control's streams the speaker reports it cannot skip, and control skips
// within its relay; on Bluetooth the phone does. 409 when the speaker is off.
WebServer::Response WebServer::skip (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"direction\": \"next\" or \"previous\"}" } }));
    }

    std::string direction;

    if (!WebJson::parseSkip (request.body, direction))
    {
        return (json (400, nlohmann::json { { "error", "expected {\"direction\": \"next\" or \"previous\"}" } }));
    }

    if (speakerState ().source == "STANDBY")
    {
        return (json (409, nlohmann::json { { "error", "the speaker is off" } }));
    }

    if (!press ((direction == "next") ? "NEXT_TRACK" : "PREV_TRACK"))
    {
        return (json (502, nlohmann::json { { "error", "the speaker did not take the key" } }));
    }

    return (json (200, nlohmann::json { { "ok", true }, { "direction", direction } }));
}

// The speaker's own sources, as its source buttons: POST {"source": "bluetooth"} or {"source": "aux"}
// selects BLUETOOTH, or AUX, through /select, and answers once the speaker is on it (504 when it is
// not in time). control takes another source as meant, and keeps out of its way; a preset brings the
// radio back.
WebServer::Response WebServer::inputSource (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"source\": \"bluetooth\" or \"aux\"}" } }));
    }

    std::string which;

    if (!WebJson::parseSource (request.body, which))
    {
        return (json (400, nlohmann::json { { "error", "expected {\"source\": \"bluetooth\" or \"aux\"}" } }));
    }

    const bool bluetooth = (which == "bluetooth");
    const std::string wanted = bluetooth ? "BLUETOOTH" : "AUX";

    {
        std::lock_guard<std::mutex> keyLock (m_keyMutex);
        SoundTouchClient client (speakerIp ());

        if (!client.selectSource (wanted, bluetooth ? std::string () : std::string ("AUX"), KEY_TIMEOUT_MS))
        {
            return (json (502, nlohmann::json { { "error", "the speaker did not take the source" } }));
        }
    }

    const bool done = waitForSpeaker ([&wanted] (const SpeakerState &state)
    {
        return (state.source == wanted);
    }, SOURCE_SETTLE);

    {
        std::lock_guard<std::mutex> lock (m_nowPlayingMutex);

        m_nowPlayingCached = false;
    }

    if (!done)
    {
        return (json (504, nlohmann::json { { "error", bluetooth ? "the speaker did not switch to Bluetooth" : "the speaker did not switch to AUX" } }));
    }

    return (json (200, nlohmann::json { { "ok", true }, { "source", which } }));
}

bool WebServer::press (const std::string &key)
{
    std::lock_guard<std::mutex> lock (m_keyMutex);
    SoundTouchClient client (speakerIp ());

    return (client.pressKey (key, KEY_TIMEOUT_MS));
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
        state.location = field ("location");

        return (state);
    }

    SoundTouchClient client (speakerIp ());
    const SoundTouchClient::NowPlaying now = client.glance (1200);

    state.source = now.source;
    state.status = now.status;
    state.location = now.location;

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
