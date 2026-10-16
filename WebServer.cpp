#include "WebServer.h"
#include "WebSocketListener.h"
#include "WebJson.h"
#include "WebAssets.h"
#include "StreamConfig.h"
#include "DeviceDiscovery.h"
#include "SpeakerConfig.h"
#include "ZoneManager.h"
#include "SoundTouchClient.h"
#include "StreamProxy.h"
#include "Say.h"
#include <pugixml.hpp>
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
#include <set>

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

    nlohmann::json targetJson (const PlaybackTarget &target)
    {
        return (nlohmann::json { { "type", target.type == PlaybackTarget::Type::Zone ? "zone" : "speaker" }, { "id", target.id } });
    }

    bool speakerConfiguration (SpeakerConfig &config, std::string &existing, std::string &error)
    {
        const int failure = readDataFile (DEVICES_FILE, existing);

        if (failure != 0 && failure != ENOENT)
        {
            error = "Cannot read devices.json: " + std::string (std::strerror (failure));
            return (false);
        }

        if (!config.loadFromText (existing.empty () ? "{}" : existing))
        {
            error = "devices.json has invalid speaker, zone or target configuration";
            return (false);
        }

        return (true);
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

    std::string error;
    SpeakerConfig config;
    std::string existing;
    if (speakerConfiguration (config, existing, error))
    {
        PlaybackTarget defaultTarget;
        if (config.hasDefaultTarget ())
        {
            defaultTarget = config.getDefaultTarget ();
        }
        else if (config.getDefaultSpeaker ())
        {
            defaultTarget = { PlaybackTarget::Type::Speaker, config.getDefaultSpeaker ()->deviceId };
        }
        ResolvedTarget resolved;
        if (config.resolveTarget (defaultTarget, resolved, error))
        {
            std::lock_guard<std::recursive_mutex> lock (m_targetMutex);
            if (m_selectedTarget.is_null ())
            {
                m_selectedTarget = targetJson (defaultTarget);
            }
            m_selectedLeaderDeviceId = resolved.leaderDeviceId;
            syncMemberWatchers (resolved);
        }
    }

    if (m_settings.backgroundDiscovery)
    {
        std::lock_guard<std::mutex> lock (m_searchMutex);
        m_searchRunning = true;
        m_searchThread = std::thread (&WebServer::searchLoop, this);
    }

    return (true);
}

void WebServer::stop ()
{
    if (!m_running.exchange (false))
    {
        return;
    }

    m_stopping = true;

    stopMemberWatchers ();

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
    const size_t limit = (request.path == "/api/streams" || request.path == "/api/zones") ? MAX_STREAMS_BODY : MAX_BODY;

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

    if (path == "/api/targets")
    {
        return ((request.method == "GET" || request.method == "HEAD") ? targets () : text (405, "Method Not Allowed"));
    }

    if (path == "/api/zones")
    {
        return (request.method == "PUT" ? saveZones (request) : text (405, "Method Not Allowed"));
    }

    if (path == "/api/target" || path == "/api/zones/dissolve" || path == "/api/member-volume")
    {
        if (request.method != "POST")
        {
            return (text (405, "Method Not Allowed"));
        }

        std::lock_guard<std::recursive_mutex> lock (m_targetMutex);

        if (path == "/api/target") return (selectTarget (request));
        if (path == "/api/zones/dissolve") return (dissolveZone (request));
        return (memberVolume (request));
    }

    // The things the dashboard changes, each a POST, and the stream list it saves with a PUT;
    // everything else is only looked at.
    if (path == "/api/volume" || path == "/api/playback" || path == "/api/power" || path == "/api/preset"
        || path == "/api/skip" || path == "/api/source" || path == "/api/select" || path == "/api/play")
    {
        if (request.method != "POST")
        {
            return (text (405, "Method Not Allowed"));
        }

        std::lock_guard<std::recursive_mutex> targetLock (m_targetMutex);

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
        std::lock_guard<std::recursive_mutex> targetLock (m_targetMutex);
        return ((request.method == "PUT") ? setDefaultSpeaker (request) : text (405, "Method Not Allowed"));
    }

    if (path == "/api/speakers/presets")
    {
        if (request.method == "POST")
        {
            return (programSpeakerPresets (request));
        }

        return ((request.method == "GET" || request.method == "HEAD") ? getSpeakerPresets (request) : text (405, "Method Not Allowed"));
    }

    if (path == "/api/speakers/dismiss-ip-change")
    {
        return ((request.method == "POST") ? dismissIpChange (request) : text (405, "Method Not Allowed"));
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

    if (path == "/speaker-details")
    {
        return (Response { 200, "text/html; charset=utf-8", SPEAKER_DETAILS_HTML, "" });
    }

    if (path == "/api/speakers/info")
    {
        return (getSpeakerInfo (request));
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

    nlohmann::json result = WebJson::speakers (saved, seen, search, ONLINE_WINDOW.count (),
                                               m_settings.embedded ? speakerIp () : std::string ());

    nlohmann::json ipChangesJson = nlohmann::json::object ();
    {
        std::lock_guard<std::mutex> ipLock (m_ipChangesMutex);
        for (const auto &pair : m_recentIpChanges)
        {
            ipChangesJson[pair.first] = ipChangeToJson (pair.second);
        }
    }
    result["ip_changes"] = ipChangesJson;

    if (result.contains ("speakers") && result["speakers"].is_array ())
    {
        for (auto &sp : result["speakers"])
        {
            if (sp.is_object () && sp.contains ("id") && sp["id"].is_string ())
            {
                const std::string spId = sp["id"].get<std::string> ();
                if (ipChangesJson.contains (spId))
                {
                    sp["ip_change"] = ipChangesJson[spId];
                }
            }
        }
    }

    return (result);
}

void WebServer::rememberSightings (SpeakerConfig &config)
{
    std::lock_guard<std::mutex> lock (m_searchMutex);
    for (const auto &[key, sighting] : m_sightings) config.remember (sighting.device);
}

void WebServer::invalidateTargetsCache ()
{
    m_targetsCached.store (false, std::memory_order_release);
}

void WebServer::invalidatePresetsCache ()
{
    m_speakersPresetsCached.store (false, std::memory_order_release);
}

void WebServer::stopMemberWatchers ()
{
    std::map<std::string, std::unique_ptr<MemberWatcher>> toStop;
    {
        std::lock_guard<std::mutex> lock (m_memberWatchersMutex);
        toStop = std::move (m_memberWatchers);
        m_memberWatchers.clear ();
    }

    for (auto &pair : toStop)
    {
        if (pair.second && pair.second->listener)
        {
            pair.second->listener->stop ();
        }
    }

    for (auto &pair : toStop)
    {
        if (pair.second && pair.second->thread.joinable ())
        {
            pair.second->thread.join ();
        }
    }
}

void WebServer::syncMemberWatchers (const ResolvedTarget &resolved)
{
    std::map<std::string, std::string> needed;

    if (resolved.isZone)
    {
        for (size_t i = 0; i < resolved.memberDeviceIds.size (); ++i)
        {
            const std::string &mid = resolved.memberDeviceIds[i];
            const std::string &mip = (i < resolved.memberIps.size ()) ? resolved.memberIps[i] : "";

            if (m_settings.embedded && mid == resolved.leaderDeviceId)
            {
                continue;
            }

            if (!mid.empty () && !mip.empty ())
            {
                needed[mid] = mip;
            }
        }
    }

    std::vector<std::unique_ptr<MemberWatcher>> toStop;

    {
        std::lock_guard<std::mutex> lock (m_memberWatchersMutex);

        for (auto it = m_memberWatchers.begin (); it != m_memberWatchers.end (); )
        {
            const std::string &mid = it->first;
            auto neededIt = needed.find (mid);
            if (neededIt == needed.end () || it->second->ip != neededIt->second)
            {
                toStop.push_back (std::move (it->second));
                it = m_memberWatchers.erase (it);
            }
            else
            {
                ++it;
            }
        }

        for (const auto &pair : needed)
        {
            const std::string &mid = pair.first;
            const std::string &mip = pair.second;

            if (m_memberWatchers.find (mid) == m_memberWatchers.end ())
            {
                auto watcher = std::make_unique<MemberWatcher> ();
                watcher->deviceId = mid;
                watcher->ip = mip;

                WebSocketListener::Callbacks cb;
                cb.volume = [this, mid] (int /* target */, int actual, bool muted)
                {
                    {
                        std::lock_guard<std::mutex> volLock (m_memberVolMutex);
                        m_memberVolumes[mid] = { actual, muted };
                    }
                    invalidateTargetsCache ();
                    if (m_liveSignal != nullptr)
                    {
                        m_liveSignal->notify ();
                    }
                };

                cb.source = [this] (const SoundTouchClient::NowPlaying &)
                {
                    invalidateTargetsCache ();
                    if (m_liveSignal != nullptr)
                    {
                        m_liveSignal->notify ();
                    }
                };

                watcher->listener = std::make_unique<WebSocketListener> (mip, cb, 0, true);

                if (watcher->listener->connect ())
                {
                    watcher->thread = std::thread ([l = watcher->listener.get ()] ()
                    {
                        l->run ();
                    });
                    m_memberWatchers[mid] = std::move (watcher);
                }
            }
        }
    }

    if (!toStop.empty ())
    {
        for (auto &watcher : toStop)
        {
            if (watcher && watcher->listener)
            {
                watcher->listener->stop ();
            }
        }

        std::thread ([stoppers = std::move (toStop)] () mutable
        {
            for (auto &watcher : stoppers)
            {
                if (watcher && watcher->thread.joinable ())
                {
                    watcher->thread.join ();
                }
            }
        }).detach ();
    }
}

nlohmann::json WebServer::memberVolumesJson () const
{
    nlohmann::json result = nlohmann::json::object ();

    {
        std::lock_guard<std::mutex> lock (m_memberVolMutex);
        for (const auto &pair : m_memberVolumes)
        {
            result[pair.first] = {
                { "volume", pair.second.actual },
                { "muted", pair.second.muted }
            };
        }
    }

    if (m_liveStatus && !m_selectedLeaderDeviceId.empty ())
    {
        const auto live = m_liveStatus ();
        if (live.contains ("volume") && !live["volume"].is_null ())
        {
            result[m_selectedLeaderDeviceId] = {
                { "volume", live["volume"] },
                { "muted", live.value ("muted", false) }
            };
        }
    }

    return (result);
}

WebServer::Response WebServer::targets ()
{
    std::lock_guard<std::recursive_mutex> targetLock (m_targetMutex);

    const auto now = std::chrono::steady_clock::now ();
    if (m_targetsCached && now - m_targetsAt < TARGETS_TTL)
    {
        return (m_targetsCache);
    }

    SpeakerConfig config;
    std::string existing;
    std::string error;

    {
        std::lock_guard<std::mutex> configLock (m_speakersMutex);

        if (!speakerConfiguration (config, existing, error))
        {
            return (json (409, { { "error", error } }));
        }
    }
    rememberSightings (config);

    PlaybackTarget defaultTarget = config.getDefaultTarget ();

    if (!config.hasDefaultTarget ())
    {
        if (const auto *device = config.getDefaultSpeaker ()) defaultTarget.id = device->deviceId;
    }

    if (m_selectedTarget.is_null ())
    {
        m_selectedTarget = targetJson (defaultTarget);

        if (m_hooks.activeSpeaker)
        {
            const std::string active = m_hooks.activeSpeaker ();
            ResolvedTarget resolved;

            if (!config.resolveTarget (defaultTarget, resolved, error) || resolved.leaderIp != active)
            {
                for (const auto &device : config.getSpeakers ())
                {
                    if (device.ipAddress == active)
                    {
                        m_selectedTarget = { { "type", "speaker" }, { "id", device.deviceId } };
                        break;
                    }
                }
            }
        }
    }

    ZoneManager manager (config);
    const auto topology = manager.snapshot (800);
    auto liveDevice = [&topology] (const std::string &id) -> const ZoneManager::DeviceLiveState *
    {
        for (const auto &device : topology.devices)
        {
            if (device.deviceId == id) return (&device);
        }

        return (nullptr);
    };
    nlohmann::json ipChangesJson = nlohmann::json::object ();
    {
        std::lock_guard<std::mutex> ipLock (m_ipChangesMutex);
        for (const auto &pair : m_recentIpChanges)
        {
            ipChangesJson[pair.first] = ipChangeToJson (pair.second);
        }
    }

    nlohmann::json speakers = nlohmann::json::array ();

    for (const auto &device : config.getSpeakers ())
    {
        const auto *live = liveDevice (device.deviceId);
        nlohmann::json spObj = { { "id", device.deviceId }, { "name", device.deviceName }, { "ip", device.ipAddress },
                                 { "online", live && live->online }, { "live_leader_id", live ? live->liveMasterId : "" } };
        if (ipChangesJson.contains (device.deviceId))
        {
            spObj["ip_change"] = ipChangesJson[device.deviceId];
        }
        speakers.push_back (spObj);
    }

    nlohmann::json zones = nlohmann::json::array ();

    for (const auto &zone : config.getZones ())
    {
        bool online = true;
        bool matching = true;
        bool grouped = false;
        std::string issue;
        std::vector<std::string> expected = zone.memberIds;
        std::sort (expected.begin (), expected.end ());

        for (const auto &id : zone.memberIds)
        {
            const auto *live = liveDevice (id);

            if (live == nullptr || !live->online)
            {
                online = false;
                issue = live ? live->error : "Speaker missing";
                continue;
            }

            grouped = grouped || live->inZone;
            std::vector<std::string> actual = live->liveFollowerIds;
            actual.push_back (live->liveMasterId);
            std::sort (actual.begin (), actual.end ());
            matching = matching && live->inZone && live->liveMasterId == zone.leaderId && actual == expected;
        }

        zones.push_back ({ { "id", zone.id }, { "name", zone.name }, { "leader_id", zone.leaderId },
                          { "members", zone.memberIds }, { "state", !online ? "unavailable" : matching ? "active" : grouped ? "different" : "inactive" },
                          { "error", issue } });
    }

    PlaybackTarget selected;
    ResolvedTarget resolved;
    nlohmann::json members = nlohmann::json::array ();

    if (SpeakerConfig::parseTargetJson (m_selectedTarget.dump (), selected, error) && config.resolveTarget (selected, resolved, error))
    {
        m_selectedLeaderDeviceId = resolved.leaderDeviceId;
        syncMemberWatchers (resolved);

        for (const auto &id : resolved.memberDeviceIds)
        {
            const auto *device = config.findById (id);
            int volActual = -1;
            bool volMuted = false;
            bool volValid = false;

            if (id == resolved.leaderDeviceId && m_liveStatus)
            {
                const auto live = m_liveStatus ();
                if (live.contains ("volume") && !live["volume"].is_null ())
                {
                    volActual = live["volume"].get<int> ();
                    volMuted = live.value ("muted", false);
                    volValid = true;
                }
            }

            if (!volValid)
            {
                std::lock_guard<std::mutex> volLock (m_memberVolMutex);
                auto it = m_memberVolumes.find (id);
                if (it != m_memberVolumes.end ())
                {
                    volActual = it->second.actual;
                    volMuted = it->second.muted;
                    volValid = true;
                }
            }

            if (!volValid)
            {
                SoundTouchClient client (device->ipAddress);
                const auto volume = client.volume (800);
                if (volume.valid)
                {
                    volActual = volume.actual;
                    volMuted = volume.muted;
                    volValid = true;
                    std::lock_guard<std::mutex> volLock (m_memberVolMutex);
                    m_memberVolumes[id] = { volume.actual, volume.muted };
                }
            }

            nlohmann::json memObj = { { "id", id }, { "name", device->deviceName }, { "ip", device->ipAddress },
                                      { "leader", id == resolved.leaderDeviceId },
                                      { "volume", volValid ? nlohmann::json (volActual) : nlohmann::json (nullptr) },
                                      { "muted", volMuted },
                                      { "error", volValid ? "" : "Volume unavailable" } };
            if (ipChangesJson.contains (id))
            {
                memObj["ip_change"] = ipChangesJson[id];
            }
            members.push_back (memObj);
        }
    }

    Response response = json (200, { { "speakers", speakers }, { "zones", zones }, { "selected", m_selectedTarget },
                                     { "default", targetJson (defaultTarget) }, { "members", members },
                                     { "ip_changes", ipChangesJson } });
    response.headers.emplace_back ("ETag", HttpUtil::entityTag (existing));
    m_targetsCache = response;
    m_targetsCached = true;
    m_targetsAt = now;
    return (response);
}

WebServer::Response WebServer::saveZones (const HttpRequest &request)
{
    if (!takesJson (request)) return (json (415, { { "error", "send JSON: {\"zones\": [...]}" } }));
    const auto document = nlohmann::json::parse (request.body, nullptr, false);

    if (!document.is_object () || !document.contains ("zones") || !document["zones"].is_array () || document["zones"].size () > 32)
    {
        return (json (400, { { "error", "send up to 32 zones in {\"zones\": [...]}" } }));
    }

    std::lock_guard<std::recursive_mutex> targetLock (m_targetMutex);
    m_targetsCached = false;
    std::lock_guard<std::mutex> configLock (m_speakersMutex);
    SpeakerConfig config;
    std::string existing;
    std::string error;
    if (!speakerConfiguration (config, existing, error)) return (json (409, { { "error", error } }));
    if (request.ifMatch.empty ()) return (json (428, { { "error", "load the zones first and send their ETag in If-Match" } }));
    if (request.ifMatch != HttpUtil::entityTag (existing)) return (json (412, { { "error", "Speaker configuration changed; reload before saving" } }));

    rememberSightings (config);

    nlohmann::ordered_json updated = nlohmann::ordered_json::parse (config.fileText (existing));
    updated["zones"] = document["zones"];
    if (updated.contains ("default_target") && updated["default_target"].value ("type", "") == "zone")
    {
        const std::string defaultId = updated["default_target"].value ("id", "");
        bool found = false;
        for (const auto &zone : document["zones"])
        {
            if (zone.is_object () && zone.contains ("id") && zone["id"] == defaultId) found = true;
        }
        if (!found) updated.erase ("default_target");
    }
    SpeakerConfig validated;
    if (!validated.loadFromText (updated.dump ())) return (json (400, { { "error", "Each zone needs a unique ID and name, a known leader, and at least two distinct known members including its leader" } }));
    for (const auto &zone : validated.getZones ())
    {
        if (zone.memberIds.size () > 16) return (json (400, { { "error", "A zone may contain at most 16 configured speakers" } }));
    }
    const std::string text = validated.fileText (updated.dump ());
    if (!StreamConfig::saveFile (DEVICES_FILE, text, existing, error)) return (json (500, { { "error", error } }));
    if (m_selectedTarget.is_object () && m_selectedTarget.value ("type", "") == "zone" &&
        validated.findZoneById (m_selectedTarget.value ("id", "")) == nullptr)
    {
        ResolvedTarget previous;
        PlaybackTarget old;
        if (SpeakerConfig::parseTargetJson (m_selectedTarget.dump (), old, error) && config.resolveTarget (old, previous, error))
            m_selectedTarget = { { "type", "speaker" }, { "id", previous.leaderDeviceId } };
        m_targetActivated = false;
    }
    if (m_liveSignal) m_liveSignal->notify ();
    Say () << ">>> Saved " << validated.getZones ().size () << " zone definitions; live grouping unchanged\n";
    return (json (200, { { "ok", true } }));
}

WebServer::Response WebServer::selectTarget (const HttpRequest &request)
{
    if (!takesJson (request)) return (json (415, { { "error", "send a JSON playback target" } }));
    PlaybackTarget target;
    std::string error;
    if (!SpeakerConfig::parseTargetJson (request.body, target, error)) return (json (400, { { "error", error } }));
    const auto body = nlohmann::json::parse (request.body);
    for (const char *flag : { "takeover", "make_default" })
    {
        if (body.contains (flag) && !body[flag].is_boolean ()) return (json (400, { { "error", std::string (flag) + " must be boolean" } }));
    }
    SpeakerConfig config;
    std::string existing;
    {
        std::lock_guard<std::mutex> lock (m_speakersMutex);
        if (!speakerConfiguration (config, existing, error)) return (json (409, { { "error", error } }));
    }
    ResolvedTarget resolved;
    rememberSightings (config);
    if (!config.resolveTarget (target, resolved, error)) return (json (404, { { "error", error } }));
    ZoneManager manager (config);
    const bool takeover = body.value ("takeover", false);
    const auto report = manager.preflight (target, takeover, 1500);
    if (!report.ready) return (json (report.conflict ? 409 : 502, { { "error", report.message } }));
    {
        std::lock_guard<std::mutex> lock (m_speakersMutex);
        std::string current;
        SpeakerConfig latest;
        if (!speakerConfiguration (latest, current, error)) return (json (409, { { "error", error } }));
        if (current != existing) return (json (409, { { "error", "Speaker configuration changed during activation; try again" } }));
        const std::string updated = config.fileText (existing);
        if (updated != existing && !StreamConfig::saveFile (DEVICES_FILE, updated, existing, error))
            return (json (500, { { "error", error } }));
    }

    std::string streamToResume;
    bool wasPlaying = false;
    if (m_liveStatus)
    {
        const auto live = m_liveStatus ();
        const std::string currentSource = live.value ("source", "");
        const std::string currentStatus = live.value ("status", "");
        wasPlaying = (currentSource == "UPNP" && (currentStatus == "PLAY_STATE" || currentStatus == "BUFFERING_STATE"))
                     || live.value ("on_control_stream", false)
                     || live.value ("want_playing", false);
        streamToResume = live.value ("station_name", "");
        if (streamToResume.empty ())
        {
            streamToResume = live.value ("last_stream", "");
        }
    }

    const std::string oldIp = speakerIp ();
    if (m_hooks.switchSpeaker && !m_hooks.switchSpeaker (resolved.leaderIp, error)) return (json (502, { { "error", error } }));
    if (!manager.activate (target, takeover, error, 6000))
    {
        std::string rollback;
        if (m_hooks.switchSpeaker && !m_hooks.switchSpeaker (oldIp, rollback)) error += "; restoring previous controller target failed: " + rollback;
        m_targetActivated = false;
        return (json (502, { { "error", error } }));
    }
    m_selectedTarget = targetJson (target);
    m_selectedLeaderDeviceId = resolved.leaderDeviceId;
    m_targetActivated = true;
    syncMemberWatchers (resolved);
    if (m_hooks.targetActivated) m_hooks.targetActivated (resolved.memberDeviceIds, resolved.leaderDeviceId);
    m_settings.defaultDeviceIp = resolved.leaderIp;
    {
        std::lock_guard<std::mutex> lock (m_nowPlayingMutex);
        m_nowPlayingCached = false;
    }
    if (body.value ("make_default", false))
    {
        std::lock_guard<std::mutex> lock (m_speakersMutex);
        SpeakerConfig latest;
        std::string current;
        if (!speakerConfiguration (latest, current, error) || !latest.setDefaultTarget (target, error) ||
            !StreamConfig::saveFile (DEVICES_FILE, latest.fileText (current), current, error))
        {
            return (json (500, { { "error", "Target selected, but default not saved: " + error } }));
        }
    }

    if (wasPlaying && !streamToResume.empty () && resolved.leaderIp != oldIp && m_hooks.playStream)
    {
        Say () << ">>> Automatically transferring playback of '" << streamToResume
               << "' to new target " << resolved.leaderIp << "...\n";
        m_hooks.playStream (streamToResume);
    }

    invalidateTargetsCache ();
    if (m_liveSignal) m_liveSignal->notify ();
    return (targets ());
}

bool WebServer::prepareTarget (std::string &error, bool takeover)
{
    SpeakerConfig config;
    std::string existing;
    {
        std::lock_guard<std::mutex> lock (m_speakersMutex);
        if (!speakerConfiguration (config, existing, error)) return (false);
    }
    rememberSightings (config);
    PlaybackTarget target;
    if (m_selectedTarget.is_null ())
    {
        target = config.getDefaultTarget ();
        if (!config.hasDefaultTarget ())
        {
            const auto *device = config.getDefaultSpeaker ();
            if (!device) { error = "No playback target configured; discover a speaker first"; return (false); }
            target.id = device->deviceId;
        }
        m_selectedTarget = targetJson (target);
    }
    if (!SpeakerConfig::parseTargetJson (m_selectedTarget.dump (), target, error)) return (false);
    ResolvedTarget resolved;
    if (!config.resolveTarget (target, resolved, error)) return (false);
    ZoneManager manager (config);
    const auto report = manager.preflight (target, takeover, 1000);
    if (!report.ready) { error = report.message + "; use the target selector to activate explicitly"; return (false); }
    if (m_targetActivated && resolved.isZone &&
        !ZoneManager::liveMembershipMatches (report.devices, resolved.memberDeviceIds, resolved.leaderDeviceId))
    {
        error = "Zone membership changed outside this dashboard; select the zone again to reactivate";
        return (false);
    }
    if (m_hooks.switchSpeaker && !m_hooks.switchSpeaker (resolved.leaderIp, error)) return (false);
    if (!manager.activate (target, takeover, error, 5000)) return (false);
    m_targetActivated = true;
    m_selectedLeaderDeviceId = resolved.leaderDeviceId;
    syncMemberWatchers (resolved);
    if (m_hooks.targetActivated) m_hooks.targetActivated (resolved.memberDeviceIds, resolved.leaderDeviceId);
    m_settings.defaultDeviceIp = resolved.leaderIp;
    invalidateTargetsCache ();
    return (true);
}

WebServer::Response WebServer::dissolveZone (const HttpRequest &request)
{
    if (!takesJson (request)) return (json (415, { { "error", "send JSON: {\"id\": zoneId}" } }));
    const auto body = nlohmann::json::parse (request.body, nullptr, false);
    if (!body.is_object () || !body.contains ("id") || !body["id"].is_string ()) return (json (400, { { "error", "send a saved zone ID" } }));
    SpeakerConfig config;
    std::string existing;
    std::string error;
    {
        std::lock_guard<std::mutex> lock (m_speakersMutex);
        if (!speakerConfiguration (config, existing, error)) return (json (409, { { "error", error } }));
    }
    const auto *zone = config.findZoneById (body["id"].get<std::string> ());
    if (!zone) return (json (404, { { "error", "No saved zone with that ID" } }));
    PlaybackTarget target { PlaybackTarget::Type::Zone, zone->id };
    ZoneManager manager (config);
    const auto report = manager.preflight (target, false, 1000);
    if (!report.ready) return (json (409, { { "error", "Live grouping differs; refusing to dissolve another zone: " + report.message } }));
    if (!manager.dissolve (zone->id, error, 6000)) return (json (502, { { "error", error } }));
    stopMemberWatchers ();
    invalidateTargetsCache ();
    if (m_liveSignal) m_liveSignal->notify ();
    return (json (200, { { "ok", true } }));
}

WebServer::Response WebServer::memberVolume (const HttpRequest &request)
{
    if (!takesJson (request)) return (json (415, { { "error", "send JSON member ID and volume, or all:true and volume" } }));
    const auto body = nlohmann::json::parse (request.body, nullptr, false);
    if (!body.is_object () || !body.contains ("volume") || !body["volume"].is_number_integer () ||
        body["volume"] < 0 || body["volume"] > 100 || (body.contains ("all") && !body["all"].is_boolean ()))
        return (json (400, { { "error", "Volume must be a whole number from 0 to 100" } }));
    const bool all = body.value ("all", false);
    if (!all && (!body.contains ("id") || !body["id"].is_string ())) return (json (400, { { "error", "Specify a member ID or all:true" } }));
    SpeakerConfig config;
    std::string existing;
    std::string error;
    {
        std::lock_guard<std::mutex> lock (m_speakersMutex);
        if (!speakerConfiguration (config, existing, error)) return (json (409, { { "error", error } }));
    }
    PlaybackTarget selected;
    ResolvedTarget resolved;
    if (m_selectedTarget.is_null ()) targets ();
    if (!SpeakerConfig::parseTargetJson (m_selectedTarget.dump (), selected, error) || !config.resolveTarget (selected, resolved, error))
        return (json (409, { { "error", error } }));
    nlohmann::json results = nlohmann::json::array ();
    bool success = true;
    const std::string requested = all ? "" : body["id"].get<std::string> ();
    if (!all && std::find (resolved.memberDeviceIds.begin (), resolved.memberDeviceIds.end (), requested) == resolved.memberDeviceIds.end ())
        return (json (404, { { "error", "Speaker is not a member of the selected target" } }));
    if (all)
    {
        const size_t count = resolved.memberDeviceIds.size ();
        std::vector<std::thread> workers;
        std::vector<bool> sents (count, false);
        const int level = body["volume"].get<int> ();
        for (size_t i = 0; i < count; ++i)
        {
            workers.emplace_back ([&resolved, &sents, i, level] () {
                SoundTouchClient client (resolved.memberIps[i]);
                sents[i] = client.setVolume (level, VOLUME_TIMEOUT_MS);
            });
        }
        for (auto &w : workers)
        {
            w.join ();
        }
        for (size_t i = 0; i < count; ++i)
        {
            const bool sent = sents[i];
            if (sent)
            {
                std::lock_guard<std::mutex> volLock (m_memberVolMutex);
                m_memberVolumes[resolved.memberDeviceIds[i]] = { level, false };
            }
            results.push_back ({ { "id", resolved.memberDeviceIds[i] }, { "ok", sent } });
            success = success && sent;
        }
    }
    else
    {
        for (size_t i = 0; i < resolved.memberDeviceIds.size (); ++i)
        {
            if (resolved.memberDeviceIds[i] != requested) continue;
            SoundTouchClient client (resolved.memberIps[i]);
            const int level = body["volume"].get<int> ();
            const bool sent = client.setVolume (level, VOLUME_TIMEOUT_MS);
            if (sent)
            {
                std::lock_guard<std::mutex> volLock (m_memberVolMutex);
                m_memberVolumes[resolved.memberDeviceIds[i]] = { level, false };
            }
            results.push_back ({ { "id", resolved.memberDeviceIds[i] }, { "ok", sent } });
            success = success && sent;
        }
    }
    invalidateTargetsCache ();
    if (m_liveSignal) m_liveSignal->notify ();
    return (json (success ? 200 : 502, { { "ok", success }, { "results", results }, { "error", success ? "" : "Some member volumes were not applied; inspect per-speaker results" } }));
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

    if (m_searchRunning)
    {
        m_searchWake.notify_all ();
        return;
    }

    if (m_stopping)
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
        bool inFocus = false;
        {
            std::lock_guard<std::mutex> lock (m_searchMutex);

            if (m_stopping)
            {
                m_searchRunning = false;
                m_searching = false;
                return;
            }

            inFocus = (std::chrono::steady_clock::now () < m_searchUntil);
            if (!inFocus && !m_settings.backgroundDiscovery)
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

        handleDiscoveredDevices (discovery.getDevices ());

        std::unique_lock<std::mutex> lock (m_searchMutex);
        if (m_stopping)
        {
            m_searchRunning = false;
            return;
        }

        const bool stillInFocus = (std::chrono::steady_clock::now () < m_searchUntil);
        const auto pauseDuration = stillInFocus
            ? SEARCH_PAUSE
            : std::chrono::seconds (std::max (1, m_settings.discoveryIntervalSeconds));

        m_searchWake.wait_for (lock, pauseDuration, [this] () { return (m_stopping.load ()); });
    }
}

void WebServer::handleDiscoveredDevices (const std::vector<SoundTouchDevice> &devices)
{
    if (devices.empty () || m_stopping)
    {
        return;
    }

    std::string error;
    SpeakerConfig config;
    std::string existing;

    std::vector<IpChangeRecord> newChanges;

    {
        std::lock_guard<std::mutex> lock (m_speakersMutex);
        if (!speakerConfiguration (config, existing, error))
        {
            return;
        }

        bool configChanged = false;

        for (const SoundTouchDevice &dev : devices)
        {
            if (dev.deviceId.empty ())
            {
                continue;
            }

            const SoundTouchDevice *saved = config.findById (dev.deviceId);
            if (saved != nullptr)
            {
                if (saved->ipAddress != dev.ipAddress)
                {
                    IpChangeRecord rec;
                    rec.deviceId = dev.deviceId;
                    rec.deviceName = dev.deviceName.empty () ? saved->deviceName : dev.deviceName;
                    rec.oldIp = saved->ipAddress;
                    rec.newIp = dev.ipAddress;
                    rec.changedAt = std::chrono::system_clock::now ();
                    rec.reconnected = false;
                    newChanges.push_back (rec);

                    Say () << ">>> [Discovery] Speaker IP change detected: " << rec.deviceName
                           << " (" << rec.deviceId << ") moved from " << rec.oldIp
                           << " to " << rec.newIp << "\n";
                }
            }

            if (config.remember (dev))
            {
                configChanged = true;
            }
        }

        if (configChanged)
        {
            const std::string text = config.fileText (existing);
            if (text != existing)
            {
                StreamConfig::saveFile (DEVICES_FILE, text, existing, error);
            }
        }
    }

    bool hasPendingReconnect = m_pendingTargetReconnect || !m_pendingResumeStream.empty ();
    if (!hasPendingReconnect)
    {
        std::lock_guard<std::mutex> ipLock (m_ipChangesMutex);
        for (const auto &pair : m_recentIpChanges)
        {
            if (!pair.second.reconnected)
            {
                hasPendingReconnect = true;
                break;
            }
        }
    }

    if (newChanges.empty () && !hasPendingReconnect)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> ipLock (m_ipChangesMutex);
        for (const auto &rec : newChanges)
        {
            m_recentIpChanges[rec.deviceId] = rec;
        }
    }

    bool affectsActiveTarget = false;
    bool leaderChanged = false;
    PlaybackTarget currentTarget;
    ResolvedTarget resolved;

    {
        std::lock_guard<std::recursive_mutex> targetLock (m_targetMutex);
        bool haveTarget = false;
        if (!m_selectedTarget.is_null ())
        {
            haveTarget = SpeakerConfig::parseTargetJson (m_selectedTarget.dump (), currentTarget, error);
        }
        if (!haveTarget)
        {
            currentTarget = config.getDefaultTarget ();
            if (!config.hasDefaultTarget ())
            {
                const auto *device = config.getDefaultSpeaker ();
                if (device) currentTarget.id = device->deviceId;
            }
            haveTarget = !currentTarget.id.empty ();
        }

        if (haveTarget && config.resolveTarget (currentTarget, resolved, error))
        {
            for (const auto &rec : newChanges)
            {
                if (rec.deviceId == resolved.leaderDeviceId)
                {
                    affectsActiveTarget = true;
                    leaderChanged = true;
                    break;
                }
                for (const auto &memId : resolved.memberDeviceIds)
                {
                    if (rec.deviceId == memId)
                    {
                        affectsActiveTarget = true;
                        break;
                    }
                }
            }

            // Also check if any unreconnected change affects this target
            if (!affectsActiveTarget)
            {
                std::lock_guard<std::mutex> ipLock (m_ipChangesMutex);
                for (const auto &pair : m_recentIpChanges)
                {
                    if (!pair.second.reconnected)
                    {
                        if (pair.second.deviceId == resolved.leaderDeviceId)
                        {
                            affectsActiveTarget = true;
                            leaderChanged = true;
                            break;
                        }
                        for (const auto &memId : resolved.memberDeviceIds)
                        {
                            if (pair.second.deviceId == memId)
                            {
                                affectsActiveTarget = true;
                                break;
                            }
                        }
                    }
                }
            }

            // Also check if controller's active speaker IP differs from resolved leader IP
            if (!affectsActiveTarget && m_hooks.activeSpeaker)
            {
                const std::string ctrlIp = m_hooks.activeSpeaker ();
                if (!ctrlIp.empty () && ctrlIp != resolved.leaderIp)
                {
                    affectsActiveTarget = true;
                    leaderChanged = true;
                }
            }

            if (!affectsActiveTarget && (!m_pendingResumeStream.empty () || m_pendingTargetReconnect))
            {
                affectsActiveTarget = true;
            }
        }
    }

    bool wasPlaying = !m_pendingResumeStream.empty ();
    std::string streamToResume = m_pendingResumeStream;
    bool alreadyPlayingOnLeader = false;

    if (m_liveStatus)
    {
        const auto live = m_liveStatus ();
        const bool wantPlaying = live.value ("want_playing", false);
        const bool waiting = live.value ("waiting_for_speaker", false);
        const std::string stationName = live.value ("station_name", "");
        const std::string lastStream = live.value ("last_stream", "");
        const std::string status = live.value ("status", "");

        if (status == "PLAY_STATE" || status == "BUFFERING_STATE")
        {
            alreadyPlayingOnLeader = true;
        }

        if (wantPlaying || waiting || alreadyPlayingOnLeader || !stationName.empty ())
        {
            wasPlaying = true;
            if (streamToResume.empty ())
            {
                streamToResume = !stationName.empty () ? stationName : lastStream;
            }
        }
    }

    if (affectsActiveTarget)
    {
        Say () << ">>> [Discovery] Active playback target affected by IP change; updating connections...\n";
        std::string prepError;
        bool ok = prepareTarget (prepError, true);
        if (ok)
        {
            Say () << ">>> [Discovery] Successfully reconnected and updated target after IP change\n";
            m_pendingTargetReconnect = false;

            {
                std::lock_guard<std::mutex> ipLock (m_ipChangesMutex);
                for (const auto &rec : newChanges)
                {
                    if (m_recentIpChanges.count (rec.deviceId))
                    {
                        m_recentIpChanges[rec.deviceId].reconnected = true;
                    }
                }
                for (auto &pair : m_recentIpChanges)
                {
                    if (pair.second.deviceId == resolved.leaderDeviceId)
                    {
                        pair.second.reconnected = true;
                    }
                    for (const auto &memId : resolved.memberDeviceIds)
                    {
                        if (pair.second.deviceId == memId)
                        {
                            pair.second.reconnected = true;
                        }
                    }
                }
            }

            // Only issue playStream if the leader IP changed or playback was interrupted/stopped
            const bool needsPlaybackStart = leaderChanged || !alreadyPlayingOnLeader;

            if (wasPlaying && !streamToResume.empty () && needsPlaybackStart)
            {
                if (m_hooks.playStream)
                {
                    Say () << ">>> [Discovery] Automatically resuming playback of '" << streamToResume
                           << "' on new IP " << resolved.leaderIp << "...\n";

                    int playRc = m_hooks.playStream (streamToResume);
                    if (playRc == 0)
                    {
                        Say () << ">>> [Discovery] Playback successfully resumed for '" << streamToResume << "'\n";
                        m_pendingResumeStream.clear ();
                    }
                    else
                    {
                        Say () << ">>> [Discovery] Warning: playStream returned " << playRc
                               << "; will retry resuming on next discovery pass\n";
                        m_pendingResumeStream = streamToResume;
                    }
                }
            }
            else
            {
                m_pendingResumeStream.clear ();
            }
        }
        else
        {
            Say () << ">>> [Discovery] Warning: could not automatically reconnect target: " << prepError
                   << "; will retry on next discovery pass\n";
            m_pendingTargetReconnect = true;
            if (wasPlaying && !streamToResume.empty ())
            {
                m_pendingResumeStream = streamToResume;
            }
        }
    }

    invalidateTargetsCache ();
    if (m_liveSignal)
    {
        m_liveSignal->notify ();
    }
}

nlohmann::json WebServer::ipChangeToJson (const IpChangeRecord &record)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
        record.changedAt.time_since_epoch ()).count ();

    return nlohmann::json {
        { "device_id", record.deviceId },
        { "device_name", record.deviceName },
        { "old_ip", record.oldIp },
        { "new_ip", record.newIp },
        { "timestamp_ms", ms },
        { "reconnected", record.reconnected }
    };
}

WebServer::Response WebServer::dismissIpChange (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, { { "error", "send JSON: {\"id\": \"<device id>\"}" } }));
    }

    std::string id;
    if (!WebJson::parseSpeakerId (request.body, id))
    {
        auto body = nlohmann::json::parse (request.body, nullptr, false);
        if (body.is_object () && body.value ("all", false))
        {
            std::lock_guard<std::mutex> lock (m_ipChangesMutex);
            m_recentIpChanges.clear ();
            invalidateTargetsCache ();
            return (json (200, { { "ok", true } }));
        }
        return (json (400, { { "error", "send {\"id\": \"<device id>\"}" } }));
    }

    {
        std::lock_guard<std::mutex> lock (m_ipChangesMutex);
        m_recentIpChanges.erase (id);
    }

    invalidateTargetsCache ();
    return (json (200, { { "ok", true }, { "id", id } }));
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
        if (!m_settings.embedded)
        {
            m_selectedTarget = nullptr;
            m_targetActivated = false;
        }

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

WebServer::Response WebServer::getSpeakerInfo (const HttpRequest &request)
{
    const std::string id = HttpUtil::queryValue (request.query, "id");
    const std::string ipQuery = HttpUtil::queryValue (request.query, "ip");

    if (id.empty () && ipQuery.empty ())
    {
        return (json (400, { { "error", "Provide speaker 'id' or 'ip' in query params" } }));
    }

    SpeakerConfig config;
    std::string existing;
    std::string error;

    {
        std::lock_guard<std::mutex> lock (m_speakersMutex);
        speakerConfiguration (config, existing, error);
    }

    rememberSightings (config);

    std::string targetIp = ipQuery;
    std::string targetId = id;
    std::string targetName;

    if (!id.empty ())
    {
        const SoundTouchDevice *device = config.findById (id);
        if (device != nullptr)
        {
            targetIp = device->ipAddress;
            targetName = device->deviceName;
        }
    }

    if (targetIp.empty ())
    {
        return (json (404, { { "error", "Speaker not found in saved or discovered devices" } }));
    }

    SoundTouchClient client (targetIp);
    std::string xml;
    long httpStatus = 0;

    if (!client.restGet ("/info", xml, 5000, false, &httpStatus) || httpStatus != 200)
    {
        return (json (502, {
            { "error", "Could not reach speaker at " + targetIp },
            { "ip", targetIp },
            { "http_status", httpStatus }
        }));
    }

    pugi::xml_document doc;
    if (!doc.load_string (xml.c_str ()))
    {
        return (json (502, { { "error", "Invalid XML received from speaker /info" } }));
    }

    pugi::xml_node infoNode = doc.child ("info");
    if (!infoNode)
    {
        return (json (502, { { "error", "Missing <info> root in speaker response" } }));
    }

    nlohmann::json res = nlohmann::json::object ();
    res["device_id"] = infoNode.attribute ("deviceID").as_string (targetId.c_str ());
    res["name"] = infoNode.child ("name").child_value (targetName.c_str ());
    res["type"] = infoNode.child ("type").child_value ();
    res["marge_account_uuid"] = infoNode.child ("margeAccountUUID").child_value ();
    res["marge_url"] = infoNode.child ("margeURL").child_value ();
    res["module_type"] = infoNode.child ("moduleType").child_value ();
    res["variant"] = infoNode.child ("variant").child_value ();
    res["variant_mode"] = infoNode.child ("variantMode").child_value ();
    res["country_code"] = infoNode.child ("countryCode").child_value ();
    res["region_code"] = infoNode.child ("regionCode").child_value ();
    res["ip"] = targetIp;

    nlohmann::json components = nlohmann::json::array ();
    for (pugi::xml_node comp : infoNode.child ("components").children ("component"))
    {
        components.push_back ({
            { "category", comp.child ("componentCategory").child_value () },
            { "software_version", comp.child ("softwareVersion").child_value () },
            { "serial_number", comp.child ("serialNumber").child_value () }
        });
    }
    res["components"] = components;

    nlohmann::json networks = nlohmann::json::array ();
    for (pugi::xml_node net : infoNode.children ("networkInfo"))
    {
        networks.push_back ({
            { "type", net.attribute ("type").as_string () },
            { "mac_address", net.child ("macAddress").child_value () },
            { "ip_address", net.child ("ipAddress").child_value () }
        });
    }
    res["network_info"] = networks;

    if (!targetId.empty ())
    {
        std::lock_guard<std::mutex> ipLock (m_ipChangesMutex);
        auto it = m_recentIpChanges.find (targetId);
        if (it != m_recentIpChanges.end ())
        {
            res["ip_change"] = ipChangeToJson (it->second);
        }
    }

    return (json (200, res));
}

WebServer::Response WebServer::getSpeakerPresets (const HttpRequest &request)
{
    (void) request;

    std::lock_guard<std::mutex> lock (m_speakersMutex);

    const auto now = std::chrono::steady_clock::now ();
    if (m_speakersPresetsCached && now - m_speakersPresetsAt < PRESETS_TTL)
    {
        return (m_speakersPresetsCache);
    }

    SpeakerConfig config;
    std::string existing;
    std::string error;

    speakerConfiguration (config, existing, error);

    rememberSightings (config);

    StreamConfig streamConfig;
    streamConfig.loadFromFile (STREAMS_FILE, true);

    const std::vector<int> requiredButtons = streamConfig.getRequiredButtons ();
    std::map<int, std::pair<std::string, std::string>> expectedPresets;

    for (const int button : requiredButtons)
    {
        const Stream *stream = streamConfig.findByPreset (button);
        if (stream == nullptr)
        {
            stream = streamConfig.findStreamUsingButton (button);
        }
        if (stream != nullptr)
        {
            expectedPresets[button] = { stream->url, stream->displayName };
        }
    }

    nlohmann::json speakerList = nlohmann::json::array ();

    for (const auto &device : config.getSpeakers ())
    {
        SoundTouchClient client (device.ipAddress);
        std::vector<SoundTouchClient::PresetItem> items;
        const bool ok = client.getPresets (items, 2000);

        std::string status = "unknown";
        nlohmann::json details = nlohmann::json::array ();

        if (!ok)
        {
            status = "unreachable";
        }
        else if (items.empty ())
        {
            status = "unprogrammed";
        }
        else
        {
            std::map<int, std::string> liveMap;
            for (const auto &item : items)
            {
                liveMap[item.id] = item.location;
            }

            bool matches = true;
            for (const auto &[button, expected] : expectedPresets)
            {
                auto it = liveMap.find (button);
                if (it == liveMap.end () || it->second != expected.first)
                {
                    matches = false;
                    break;
                }
            }

            status = matches ? "synced" : "mismatch";
        }

        for (const auto &item : items)
        {
            details.push_back ({ { "id", item.id }, { "name", item.name }, { "location", item.location } });
        }

        speakerList.push_back ({
            { "id", device.deviceId },
            { "name", device.deviceName },
            { "ip", device.ipAddress },
            { "status", status },
            { "presets", details }
        });
    }

    PlaybackTarget activeTarget;
    ResolvedTarget resolved;
    std::string activeStatus = "unknown";
    std::string activeTargetName = "";

    if (m_selectedTarget.is_null ())
    {
        activeTarget = config.getDefaultTarget ();
        if (!config.hasDefaultTarget ())
        {
            const auto *device = config.getDefaultSpeaker ();
            if (device)
            {
                activeTarget.id = device->deviceId;
            }
        }
    }
    else
    {
        SpeakerConfig::parseTargetJson (m_selectedTarget.dump (), activeTarget, error);
    }

    if (!activeTarget.id.empty () && config.resolveTarget (activeTarget, resolved, error))
    {
        activeTargetName = resolved.isZone ? ("Zone: " + resolved.zoneName) : resolved.leaderDeviceName;
        for (const auto &s : speakerList)
        {
            if (s.value ("id", "") == resolved.leaderDeviceId)
            {
                activeStatus = s.value ("status", "unknown");
                break;
            }
        }
    }

    nlohmann::json expectedJson = nlohmann::json::array ();
    for (const auto &[button, exp] : expectedPresets)
    {
        expectedJson.push_back ({ { "button", button }, { "url", exp.first }, { "name", exp.second } });
    }

    Response res = json (200, {
        { "speakers", speakerList },
        { "active_target", {
            { "type", resolved.isZone ? "zone" : "speaker" },
            { "id", activeTarget.id },
            { "name", activeTargetName },
            { "leader_id", resolved.leaderDeviceId },
            { "leader_ip", resolved.leaderIp },
            { "status", activeStatus }
        }},
        { "expected_presets", expectedJson }
    });
    m_speakersPresetsCache = res;
    m_speakersPresetsCached = true;
    m_speakersPresetsAt = now;
    return (res);
}

WebServer::Response WebServer::programSpeakerPresets (const HttpRequest &request)
{
    invalidatePresetsCache ();
    if (!takesJson (request))
    {
        return (json (415, { { "error", "send JSON: {\"id\": \"<device id>\"} or {\"all\": true}" } }));
    }

    const auto body = nlohmann::json::parse (request.body, nullptr, false);
    if (!body.is_object () || (!body.contains ("id") && !body.contains ("all")))
    {
        return (json (400, { { "error", "Specify a speaker ID in 'id' or 'all': true" } }));
    }

    const bool all = body.value ("all", false);
    const std::string requestedId = all ? "" : body.value ("id", "");

    SpeakerConfig config;
    std::string existing;
    std::string error;

    {
        std::lock_guard<std::mutex> lock (m_speakersMutex);
        if (!speakerConfiguration (config, existing, error))
        {
            return (json (409, { { "error", error } }));
        }
    }

    rememberSightings (config);

    StreamConfig streamConfig;
    if (!streamConfig.loadFromFile (STREAMS_FILE, true))
    {
        return (json (500, { { "error", "Could not load streams.json" } }));
    }

    const std::vector<int> buttons = streamConfig.getRequiredButtons ();
    if (buttons.empty ())
    {
        return (json (400, { { "error", "No streams in streams.json declare a preset" } }));
    }

    std::vector<SoundTouchDevice> targets;
    if (all)
    {
        targets = config.getSpeakers ();
        if (targets.empty ())
        {
            return (json (404, { { "error", "No speakers configured or discovered" } }));
        }
    }
    else
    {
        const SoundTouchDevice *dev = config.findById (requestedId);
        if (!dev)
        {
            return (json (404, { { "error", "Speaker not found: " + requestedId } }));
        }
        targets.push_back (*dev);
    }

    nlohmann::json results = nlohmann::json::array ();
    bool overallSuccess = true;

    for (const auto &target : targets)
    {
        SoundTouchClient client (target.ipAddress);
        int programmedCount = 0;
        int failedCount = 0;
        std::vector<std::string> errors;

        for (const int button : buttons)
        {
            const Stream *stream = streamConfig.findByPreset (button);
            if (stream == nullptr)
            {
                stream = streamConfig.findStreamUsingButton (button);
            }
            if (stream == nullptr)
            {
                continue;
            }

            if (client.savePreset (button, stream->url, stream->displayName))
            {
                ++programmedCount;
            }
            else
            {
                ++failedCount;
                errors.push_back ("Button " + std::to_string (button) + " failed");
            }
        }

        const bool devOk = (failedCount == 0 && programmedCount > 0);
        if (!devOk)
        {
            overallSuccess = false;
        }

        results.push_back ({
            { "id", target.deviceId },
            { "name", target.deviceName },
            { "ip", target.ipAddress },
            { "ok", devOk },
            { "programmed", programmedCount },
            { "failed", failedCount },
            { "errors", errors }
        });

        Say () << ">>> Programmed presets for speaker " << target.deviceId << " ("
               << target.deviceName << ") at " << target.ipAddress << ": "
               << programmedCount << " succeeded, " << failedCount << " failed\n";
    }

    return (json (overallSuccess ? 200 : 207, {
        { "ok", overallSuccess },
        { "results", results },
        { "error", overallSuccess ? "" : "Some preset programming attempts failed" }
    }));
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
    json["background_discovery"] = m_settings.backgroundDiscovery;
    json["discovery_interval_seconds"] = m_settings.discoveryIntervalSeconds;

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
        nlohmann::json json = m_liveStatus ();
        json["member_volumes"] = memberVolumesJson ();
        return (json);
    }

    {
        std::lock_guard<std::mutex> lock (m_nowPlayingMutex);

        if (m_nowPlayingCached && std::chrono::steady_clock::now () - m_nowPlayingAt < NOW_PLAYING_TTL)
        {
            nlohmann::json json = m_nowPlayingCache;
            json["member_volumes"] = memberVolumesJson ();
            return (json);
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

    json["member_volumes"] = memberVolumesJson ();
    return (json);
}

nlohmann::json WebServer::liveJson () const
{
    nlohmann::json json = m_liveStatus ? m_liveStatus () : nlohmann::json::object ();
    json["member_volumes"] = memberVolumesJson ();
    return (json);
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

    body["member_volumes"] = memberVolumesJson ();
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

    if (m_selectedTarget.is_null ()) targets ();
    if (m_selectedTarget.value ("type", "") == "zone")
    {
        SpeakerConfig config;
        std::string existing;
        std::string error;
        {
            std::lock_guard<std::mutex> lock (m_speakersMutex);
            if (!speakerConfiguration (config, existing, error)) return (json (409, { { "error", error } }));
        }
        PlaybackTarget target;
        ResolvedTarget resolved;
        if (!SpeakerConfig::parseTargetJson (m_selectedTarget.dump (), target, error) || !config.resolveTarget (target, resolved, error))
            return (json (409, { { "error", error } }));
        nlohmann::json results = nlohmann::json::array ();
        bool success = true;
        const size_t count = resolved.memberIps.size ();
        std::vector<std::thread> workers;
        std::vector<bool> sents (count, false);
        for (size_t i = 0; i < count; ++i)
        {
            workers.emplace_back ([&resolved, &sents, i, level] () {
                SoundTouchClient client (resolved.memberIps[i]);
                sents[i] = client.setVolume (level, VOLUME_TIMEOUT_MS);
            });
        }
        for (auto &w : workers)
        {
            w.join ();
        }
        for (size_t i = 0; i < count; ++i)
        {
            const bool sent = sents[i];
            results.push_back ({ { "id", resolved.memberDeviceIds[i] }, { "ok", sent } });
            if (sent)
            {
                std::lock_guard<std::mutex> volLock (m_memberVolMutex);
                m_memberVolumes[resolved.memberDeviceIds[i]] = { level, false };
            }
            success = success && sent;
        }
        invalidateTargetsCache ();
        {
            std::lock_guard<std::mutex> lock (m_nowPlayingMutex);
            m_nowPlayingCached = false;
        }
        if (m_liveSignal) m_liveSignal->notify ();
        return (json (success ? 200 : 502, { { "ok", success }, { "volume", level }, { "results", results },
                                            { "error", success ? "" : "Some zone member volumes were not applied" } }));
    }

    SoundTouchClient client (speakerIp ());

    if (!client.setVolume (level, VOLUME_TIMEOUT_MS))
    {
        return (json (502, nlohmann::json { { "error", "the speaker did not take it" } }));
    }

    if (!m_selectedLeaderDeviceId.empty ())
    {
        std::lock_guard<std::mutex> volLock (m_memberVolMutex);
        m_memberVolumes[m_selectedLeaderDeviceId] = { level, false };
    }
    invalidateTargetsCache ();

    // The standalone server's cached look at the speaker is out of date now.
    {
        std::lock_guard<std::mutex> lock (m_nowPlayingMutex);

        m_nowPlayingCached = false;
    }
    if (m_liveSignal) m_liveSignal->notify ();

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

    if (action == "play")
    {
        std::string error;
        if (!prepareTarget (error)) return (json (409, { { "error", error } }));
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

    if (m_selectedTarget.is_null ()) targets ();
    if (m_selectedTarget.value ("type", "") == "zone")
    {
        SpeakerConfig config;
        std::string existing;
        std::string error;
        {
            std::lock_guard<std::mutex> lock (m_speakersMutex);
            if (!speakerConfiguration (config, existing, error)) return (json (409, { { "error", error } }));
        }
        PlaybackTarget target;
        ResolvedTarget resolved;
        if (!SpeakerConfig::parseTargetJson (m_selectedTarget.dump (), target, error) || !config.resolveTarget (target, resolved, error))
            return (json (409, { { "error", error } }));
        nlohmann::json results = nlohmann::json::array ();
        bool success = true;
        // Followers first on power-off, so the leader cannot disappear before their commands.
        for (size_t i = resolved.memberIps.size (); i-- > 0;)
        {
            SoundTouchClient client (resolved.memberIps[i]);
            auto state = client.glance (1000);
            bool ok = !state.source.empty ();
            if (ok && ((state.source != "STANDBY") != on)) ok = client.pressKey ("POWER", KEY_TIMEOUT_MS);
            const auto deadline = std::chrono::steady_clock::now () + POWER_SETTLE;
            while (ok && std::chrono::steady_clock::now () < deadline)
            {
                state = client.glance (1000);
                if (!state.source.empty () && ((state.source != "STANDBY") == on)) break;
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
            }
            ok = ok && !state.source.empty () && ((state.source != "STANDBY") == on);
            success = success && ok;
            results.push_back ({ { "id", resolved.memberDeviceIds[i] }, { "ok", ok } });
        }
        if (m_liveSignal) m_liveSignal->notify ();
        return (json (success ? 200 : 502, { { "ok", success }, { "on", on }, { "results", results },
                                            { "error", success ? "" : "Some zone speakers did not change power state" } }));
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

    std::string error;
    if (!prepareTarget (error)) return (json (409, { { "error", error } }));

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
    std::string error;
    if (!prepareTarget (error)) return (json (409, { { "error", error } }));
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
    std::string error;
    if (!prepareTarget (error)) return (json (409, { { "error", error } }));
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
        invalidatePresetsCache ();
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

    std::string error;
    if (!prepareTarget (error)) return (json (409, { { "error", error } }));

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
// radio back. AirPlay is initiated from the Apple source device and cannot be selected via /select.
WebServer::Response WebServer::inputSource (const HttpRequest &request)
{
    if (!takesJson (request))
    {
        return (json (415, nlohmann::json { { "error", "send JSON: {\"source\": \"bluetooth\" or \"aux\"}" } }));
    }

    std::string which;

    if (!WebJson::parseSource (request.body, which))
    {
        // Check if user specifically requested AirPlay
        nlohmann::json reqJson = nlohmann::json::parse (request.body, nullptr, false);
        if (!reqJson.is_discarded () && reqJson.is_object () && reqJson.value ("source", "") == "airplay")
        {
            return (json (400, nlohmann::json { { "error", "AirPlay streaming must be started from an Apple device (iPhone, iPad, or Mac)." } }));
        }
        return (json (400, nlohmann::json { { "error", "expected {\"source\": \"bluetooth\" or \"aux\"}" } }));
    }

    std::string error;
    if (!prepareTarget (error)) return (json (409, { { "error", error } }));

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
    std::lock_guard<std::recursive_mutex> lock (m_targetMutex);
    if (m_hooks.activeSpeaker) return (m_hooks.activeSpeaker ());
    if (m_selectedTarget.is_object ())
    {
        SpeakerConfig config;
        std::string text;
        std::string error;
        PlaybackTarget target;
        ResolvedTarget resolved;
        if (speakerConfiguration (config, text, error) && SpeakerConfig::parseTargetJson (m_selectedTarget.dump (), target, error) &&
            config.resolveTarget (target, resolved, error)) return (resolved.leaderIp);
        Say (std::cerr) << "Cannot resolve selected playback target: " << error << "\n";
        return ("");
    }
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
