#include "SoundTouchClient.h"
#include "WebSocketListener.h"
#include "StreamConfig.h"
#include "DeviceDiscovery.h"
#include "IcyReader.h"
#include "StreamProxy.h"
#include "WebServer.h"
#include "LiveSignal.h"
#include "Say.h"
#include <curl/curl.h>
#include <mutex>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <thread>
#include <chrono>
#include <cstdint>
#include <limits>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <deque>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unistd.h>

// The relay's port unless --relay-port or SOUNDTOUCH_RELAY_PORT says otherwise. With host
// networking there is no remapping it, so it has to be settable where 8899 is taken.
static constexpr int DEFAULT_RELAY_PORT = 8899;

// The web dashboard's port unless --web-port or SOUNDTOUCH_WEB_PORT says otherwise.
static constexpr int DEFAULT_WEB_PORT = 8081;

// Reported by the dashboard and /api/health.
static constexpr const char *WEB_VERSION = "cxstcc/0.1";

// From a cold connection a station's first title takes ~2.2s (the CDN's redirect, TLS, then 4 KB
// of audio), and the rest of its opening backlog is in ~0.8s after its first byte, so allow for a
// slow start with some margin.
static constexpr std::chrono::milliseconds FIRST_TITLE_WAIT (4500);

// A title that is no longer the one playing this much later is a station ID, or a reconnect
// replaying the song before; neither is worth a gap on the speaker.
static constexpr long MIN_TITLE_SECONDS = 10;

static constexpr std::chrono::milliseconds WATCH_TICK (100);

// The last preset played, remembered so control can bring it back.
static constexpr const char *STATE_FILE = "state.json";

// A song push this recent is to blame when the speaker gives up, so recovering from that is
// control's job whatever the resume setting.
static constexpr std::chrono::seconds PUSH_BLAME_WINDOW (15);

// At most this many automatic resumes within RESUME_LIMIT_WINDOW; past that something is wrong
// that playing again will not fix.
static constexpr size_t RESUME_LIMIT = 3;
static constexpr std::chrono::minutes RESUME_LIMIT_WINDOW (10);

// A pause or stop this soon after control itself sent a command may be that command's doing: the
// speaker can report one on its way to playing what it was sent. It is the user's if the speaker
// has not gone on to buffer or play by the end of the window, and at least PAUSE_CONFIRM after it.
static constexpr std::chrono::seconds OWN_TRANSITION_WINDOW (3);
static constexpr std::chrono::seconds PAUSE_CONFIRM (1);

// Found paused at start-up or on reconnecting, the speaker counts as paused by the user only if it
// still is this much later.
static constexpr std::chrono::seconds PAUSE_SETTLE (2);

// After a reboot the speaker's event stream can be up before its REST and UPnP services are.
static constexpr std::chrono::seconds SPEAKER_READY_WAIT (20);

// A speaker started again after asking for a pause point the relay no longer holds can ask once
// more before that start reaches it; one start is enough.
static constexpr std::chrono::seconds RESTART_SETTLE (4);

// The remote's previous button pressed again this soon after the last one took the speaker back
// goes to the song before, however far into the song it is: each press costs a gap of a second or
// two and the speaker's own "action unavailable", so a second press rarely comes within the
// usual few seconds of the song's start.
static constexpr std::chrono::seconds PREVIOUS_AGAIN (10);

// A push to the display that failed is tried again after this.
static constexpr std::chrono::seconds PUSH_RETRY (2);

// Without the relay, a title is pushed once no newer one has followed it for this long. That lets
// a connection's opening backlog, which can begin in one song and end in the next, pass.
static constexpr std::chrono::milliseconds DIRECT_SETTLE (1500);

// Without the relay, how long a title waits for the speaker to be back on the stream before it
// is dropped.
static constexpr std::chrono::seconds DIRECT_GIVE_UP (15);

// Control's listener while it runs, for the signal thread; owned by control. Not a smart pointer,
// so that nothing writes it during static destruction while the signal thread might be reading.
static WebSocketListener *g_listener = nullptr;

static long long steadyMilliseconds ()
{
    return (std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now ().time_since_epoch ()).count ());
}

static std::chrono::steady_clock::time_point steadyTimeFromMilliseconds (long long milliseconds)
{
    return (std::chrono::steady_clock::time_point (
        std::chrono::duration_cast<std::chrono::steady_clock::duration> (std::chrono::milliseconds (milliseconds))));
}

// The whole of text as a whole number, or false: "9900x", "1e4" and "" are not numbers.
static bool parseWholeNumber (const char *text, long &value)
{
    char *end = nullptr;

    errno = 0;
    value = std::strtol (text, &end, 10);

    return (end != text && *end == '\0' && errno != ERANGE);
}

// The whole of text as a finite number of seconds, or false.
static bool parseSeconds (const char *text, double &value)
{
    char *end = nullptr;

    errno = 0;
    value = std::strtod (text, &end);

    return (end != text && *end == '\0' && errno != ERANGE && std::isfinite (value));
}

// A ContentItem location that names a stream. Handed one without DIDL-Lite metadata, the speaker
// reports this placeholder instead.
static bool isStreamLocation (const std::string &location)
{
    return (!location.empty () && location != "unplayable location");
}

// What played last, to bring back: its name and its preset (0 for none). A state.json from before
// streams were remembered by name has only the preset.
static void loadLastPlayed (int &preset, std::string &name)
{
    preset = 0;
    name.clear ();

    std::ifstream ifs (STATE_FILE);

    if (!ifs.is_open ())
    {
        return;
    }

    try
    {
        const nlohmann::json state = nlohmann::json::parse (ifs);

        preset = state.value ("last_preset", 0);
        name = state.value ("last_stream", std::string ());
    }
    catch (const nlohmann::json::exception &)
    {
        preset = 0;
        name.clear ();
    }
    catch (const std::exception &e)
    {
        // Such as a directory where the file should be, as a docker bind mount of a missing file makes.
        Say (std::cerr) << "Warning: cannot read " << STATE_FILE << ": " << e.what () << "\n";
        preset = 0;
        name.clear ();
    }
}

static void saveLastPlayed (int preset, const std::string &name)
{
    errno = 0;

    std::ofstream ofs (STATE_FILE);
    int failure = ofs.is_open () ? 0 : (errno != 0 ? errno : EIO);

    if (failure == 0)
    {
        errno = 0;

        ofs << nlohmann::json { { "last_preset", preset }, { "last_stream", name } }.dump (2) << "\n";
        ofs.close ();

        if (ofs.fail ())
        {
            failure = (errno != 0) ? errno : EIO;
        }
    }

    // Losing it only loses the resume at the next start, but say so: in a container, a data
    // directory the image's user cannot write is an easy mistake.
    if (failure != 0)
    {
        char directory[4096] = ".";

        if (getcwd (directory, sizeof (directory)) == nullptr)
        {
            std::strcpy (directory, ".");
        }

        Say (std::cerr) << "Warning: cannot save " << STATE_FILE << " in " << directory << ": "
                        << std::strerror (failure) << "; what played last will not be remembered.\n";
    }
}

static std::mutex g_listenerMutex;              // g_listener, between main and the signal thread
static std::atomic<bool> g_stopRequested (false);   // for loops that have no listener to stop

// Ctrl-C, docker stop or systemd (SIGTERM), and a closed terminal (SIGHUP) all mean "stop". They
// are blocked in every thread and taken here, by sigwait, on an ordinary thread: so the stop can do
// whatever it needs to, rather than only what is safe inside a signal handler. The first asks the
// run loop to return, so main can unwind and wait for any playback in flight; a second gives up on
// that and quits at once. Must be called before any other thread starts, so they all inherit the
// blocked set.
static void handleStopSignals ()
{
    static sigset_t stopSignals;

    sigemptyset (&stopSignals);
    sigaddset (&stopSignals, SIGINT);
    sigaddset (&stopSignals, SIGTERM);

    // Started under nohup, a closed terminal is not a stop: blocking SIGHUP here would still deliver it.
    struct sigaction hangup {};

    if (sigaction (SIGHUP, nullptr, &hangup) != 0 || hangup.sa_handler != SIG_IGN)
    {
        sigaddset (&stopSignals, SIGHUP);
    }

    pthread_sigmask (SIG_BLOCK, &stopSignals, nullptr);

    std::thread ([] ()
    {
        int received = 0;

        sigwait (&stopSignals, &received);

        Say (std::cerr) << "\nStopping (" << (received == SIGINT ? "Ctrl-C" : received == SIGTERM ? "SIGTERM" : "SIGHUP")
                        << "); send it again to quit at once.\n";

        g_stopRequested = true;

        {
            std::lock_guard<std::mutex> lock (g_listenerMutex);

            if (g_listener != nullptr)
            {
                g_listener->stop ();
            }
        }

        sigwait (&stopSignals, &received);

        Say (std::cerr) << "\nQuitting now.\n";
        std::_Exit (128 + received);
    }).detach ();
}

// Renders preset 231 as "2 then 3 then 1".
static std::string describeSequence (int preset)
{
    std::vector<int> digits;

    for (int value = preset; value != 0; value /= 10)
    {
        digits.push_back (value % 10);
    }

    std::string text;

    for (auto digit = digits.rbegin (); digit != digits.rend (); ++digit)
    {
        if (!text.empty ())
        {
            text += " then ";
        }

        text += static_cast<char> ('0' + *digit);
    }

    return (text);
}

// "Artist - Song" is the Shoutcast convention; anything else is taken as all song.
static void splitStreamTitle (const std::string &title, std::string &artist, std::string &song)
{
    const size_t dash = title.find (" - ");

    artist = (dash == std::string::npos) ? std::string () : title.substr (0, dash);
    song = (dash == std::string::npos) ? title : title.substr (dash + 3);
}

// Through the relay the speaker reports the relay's URL rather than the station's, but the
// station's name rides in the path.
static const Stream *findPlayingStream (const StreamConfig &config, const std::string &url)
{
    const Stream *stream = config.findByUrl (url);

    if (stream != nullptr)
    {
        return (stream);
    }

    // Whatever port that control's relay was started on.
    const std::string name = StreamProxy::streamNameFromUrl (url, 0);

    return (name.empty () ? nullptr : config.findByName (name));
}

// One of a station list's streams, kept alive along with the list, however long it is held: the
// dashboard can swap in a new list at any moment.
static std::shared_ptr<const Stream> holdStream (const std::shared_ptr<const StreamConfig> &streams, const Stream *stream)
{
    return ((stream != nullptr) ? std::shared_ptr<const Stream> (streams, stream) : nullptr);
}

// The same stream, as far as playing it goes, even from different versions of the list.
static bool sameStream (const std::shared_ptr<const Stream> &a, const std::shared_ptr<const Stream> &b)
{
    return (a != nullptr && b != nullptr && a->name == b->name && a->url == b->url);
}

// Brief if, MIN_TITLE_SECONDS on, a different title is playing. That catches a station ID between
// two songs, and lets a song through when an ID interrupting it is over by then. It is judged on
// what has arrived, so it never waits for more.
static bool isBriefTitle (const StreamProxy::Timeline &timeline, const StreamProxy::TitleMark &mark)
{
    const std::uint64_t rate = static_cast<std::uint64_t> (timeline.bytesPerSecond);
    const std::uint64_t horizon = std::min (timeline.written, mark.offset + rate * MIN_TITLE_SECONDS);
    std::string lasting = mark.title;

    for (const StreamProxy::TitleMark &later : timeline.upcoming)
    {
        if (later.offset < mark.offset)
        {
            continue;
        }

        if (later.offset > horizon)
        {
            break;
        }

        lasting = later.title;
    }

    return (lasting != mark.title);
}

// Where the push for a title belongs, in stream bytes: the station's title change moved by
// --title-offset seconds, later when positive and earlier when negative, never before byte 0.
// Not capped at what has arrived: a point beyond it is simply not reached yet.
static std::uint64_t pushPoint (std::uint64_t titleOffset, double offsetSeconds, long bytesPerSecond)
{
    const double shifted = static_cast<double> (titleOffset) + offsetSeconds * static_cast<double> (bytesPerSecond);

    return ((shifted <= 0) ? 0 : static_cast<std::uint64_t> (std::llround (shifted)));
}

// Whether a push can be moved this much earlier. Only as far as the relay sees ahead of the speaker:
// how far this title's change is ahead of where the speaker is playing when the title arrives.
static bool earlierFits (double offsetSeconds, long bytesPerSecond, std::uint64_t titleOffset,
                         std::uint64_t speakerPosition)
{
    if (offsetSeconds >= 0)
    {
        return (true);
    }

    const double lead = (titleOffset > speakerPosition) ? static_cast<double> (titleOffset - speakerPosition) : 0.0;

    return (-offsetSeconds * static_cast<double> (bytesPerSecond) <= lead);
}

static void printUsage (const char *programName)
{
    Say () << "Usage:\n";
    Say () << "  " << programName << " discover [--save]\n";
    Say () << "  " << programName << " devices\n";
    Say () << "  " << programName << " set-default <device-id>\n";
    Say () << "  " << programName << " list\n";
    Say () << "  " << programName << " play <stream-name>\n";
    Say () << "  " << programName << " stop\n";
    Say () << "  " << programName << " status\n";
    Say () << "  " << programName << " nowplaying [--watch] [--update-track-info]"
           << " [--interval <s>] [stream]\n";
    Say () << "  " << programName << " presets\n";
    Say () << "  " << programName << " program-presets\n";
    Say () << "  " << programName << " save <preset> [stream-name]\n";
    Say () << "  " << programName << " select <preset>\n";
    Say () << "  " << programName << " control [--update-track-info] [--no-proxy] [--no-resume]"
           << " [--relay-port <port>]\n";
    Say () << "          [--title-offset <seconds>]   push song titles later (+) or earlier (-)\n";
    Say () << "          [--relay-buffer <MB>]        stream kept by the relay, which is how long a\n";
    Say () << "                                       pause still carries on exactly where it stopped\n";
    Say () << "          [--web] [--web-port <port>] [--web-bind <addr>]  serve the dashboard too\n";
    Say () << "  " << programName << " web [--web-port <port>] [--web-bind <addr>]"
           << "   the dashboard, on its own\n";
    Say () << "\n";
    Say () << "  --data-dir <dir> before any command, or SOUNDTOUCH_DATA_DIR, sets where streams.json,\n";
    Say () << "  devices.json and state.json live (default: the current directory).\n";
    Say () << "  SOUNDTOUCH_RELAY_PORT sets control's relay port (default " << DEFAULT_RELAY_PORT << "),\n";
    Say () << "  SOUNDTOUCH_RELAY_BUFFER its --relay-buffer (default " << StreamProxy::DEFAULT_BUFFER_MB
           << " MB, pauses of ~" << StreamProxy::pauseMinutes (StreamProxy::DEFAULT_BUFFER_MB * 1024 * 1024, 8000)
           << " min at 64 kbps; "
           << StreamProxy::MIN_BUFFER_MB << " to " << StreamProxy::MAX_BUFFER_MB << "),\n";
    Say () << "  SOUNDTOUCH_TITLE_OFFSET its --title-offset.\n";
    Say () << "  SOUNDTOUCH_WEB_PORT sets the dashboard port (default " << DEFAULT_WEB_PORT
           << "), SOUNDTOUCH_WEB_BIND its bind address.\n";
    Say () << "\n";
    Say () << "Examples:\n";
    Say () << "  " << programName << " discover --save # Find devices and save to devices.json\n";
    Say () << "  " << programName << " devices         # Show saved devices\n";
    Say () << "  " << programName << " set-default <id># Set default device\n";
    Say () << "  " << programName << " list            # List all available streams\n";
    Say () << "  " << programName << " play klove      # Play K-LOVE main\n";
    Say () << "  " << programName << " play air1       # Play Air1\n";
    Say () << "  " << programName << " play klove-90s  # Play K-LOVE 90s\n";
    Say () << "  " << programName << " program-presets # Store every preset declared in streams.json\n";
    Say () << "  " << programName << " save 1          # Save the playing stream to preset 1\n";
    Say () << "  " << programName << " save 4 klove-90s # Save a named stream to preset 4\n";
    Say () << "  " << programName << " status          # Check what's playing\n";
    Say () << "  " << programName << " nowplaying      # Current song on the playing stream\n";
    Say () << "  " << programName << " nowplaying --watch  # Print each song as it changes\n";
    Say () << "  " << programName << " nowplaying --update-track-info\n";
    Say () << "                               # ...and show it on the speaker (gap per song)\n";
    Say () << "  " << programName << " nowplaying --interval 30\n";
    Say () << "                               # sample every 30s instead of staying connected\n";
    Say () << "  " << programName << " control         # Watch preset buttons and take over playback\n";
    Say () << "  " << programName << " web             # Dashboard on port " << DEFAULT_WEB_PORT << "\n";
    Say () << "  " << programName << " control --web   # ...and the dashboard alongside control\n";
}

static int handleCommand (int argc, char *argv[])
{
    if (argc < 2)
    {
        printUsage (argv[0]);
        return (1);
    }

    const std::string command (argv[1]);

    // Needs no configuration, so it works on an empty data directory too.
    if (command == "help" || command == "-h" || command == "--help")
    {
        printUsage (argv[0]);
        return (0);
    }

    if (command == "discover")
    {
        DeviceDiscovery discovery;

        Say () << "\n";

        if (!discovery.discover (3))
        {
            Say (std::cerr) << "No SoundTouch devices found on the network\n";
            Say (std::cerr) << "Make sure your SoundTouch device is powered on and connected\n";
            return (1);
        }

        discovery.printDevices ();

        const bool shouldSave = (argc >= 3 && std::string (argv[2]) == "--save");

        if (shouldSave)
        {
            if (!discovery.saveToFile ("devices.json"))
            {
                Say (std::cerr) << "Failed to save devices\n";
                return (1);
            }
        }
        else
        {
            Say () << "\nTo save these devices, run: " << argv[0] << " discover --save\n";
        }

        return (0);
    }

    if (command == "devices")
    {
        DeviceDiscovery discovery;

        if (!discovery.loadFromFile ("devices.json"))
        {
            Say (std::cerr) << "No devices.json found\n";
            Say (std::cerr) << "Run '" << argv[0] << " discover --save' first\n";
            return (1);
        }

        discovery.printDevices ();

        const SoundTouchDevice *defaultDevice = discovery.getDefaultDevice ();

        if (defaultDevice != nullptr)
        {
            Say () << "Default device: " << defaultDevice->deviceId;

            if (!defaultDevice->deviceName.empty ())
            {
                Say () << " (" << defaultDevice->deviceName << ")";
            }

            Say () << "\n";
        }

        return (0);
    }

    if (command == "set-default")
    {
        if (argc < 3)
        {
            Say (std::cerr) << "Error: device ID required\n";
            Say (std::cerr) << "Usage: " << argv[0] << " set-default <device-id>\n";
            Say (std::cerr) << "Run '" << argv[0] << " devices' to see available devices\n";
            return (1);
        }

        const std::string deviceId (argv[2]);

        DeviceDiscovery discovery;

        if (!discovery.loadFromFile ("devices.json"))
        {
            Say (std::cerr) << "No devices.json found\n";
            Say (std::cerr) << "Run '" << argv[0] << " discover --save' first\n";
            return (1);
        }

        if (!discovery.setDefaultDevice (deviceId))
        {
            Say (std::cerr) << "Error: Device ID '" << deviceId << "' not found\n";
            Say (std::cerr) << "Run '" << argv[0] << " devices' to see available devices\n";
            return (1);
        }

        if (!discovery.saveToFile ("devices.json", deviceId))
        {
            Say (std::cerr) << "Failed to save configuration\n";
            return (1);
        }

        Say () << "Default device set to: " << deviceId << "\n";

        return (0);
    }

    // The dashboard reads its data fresh on each request, so it needs no configuration up front and
    // works on an empty data directory; handled here, before the shared load that insists on streams.
    if (command == "web")
    {
        long webPort = DEFAULT_WEB_PORT;
        std::string webBind;

        auto webEnv = [] (const char *name) -> const char *
        {
            const char *value = std::getenv (name);

            return ((value != nullptr && *value != '\0') ? value : nullptr);
        };

        if (const char *text = webEnv ("SOUNDTOUCH_WEB_PORT"))
        {
            if (!parseWholeNumber (text, webPort) || webPort < 1 || webPort > 65535)
            {
                Say (std::cerr) << "Error: SOUNDTOUCH_WEB_PORT takes a port from 1 to 65535, not '" << text << "'\n";
                return (1);
            }
        }

        if (const char *text = webEnv ("SOUNDTOUCH_WEB_BIND"))
        {
            webBind = text;
        }

        for (int i = 2; i < argc; ++i)
        {
            std::string arg (argv[i]);
            std::string inlineValue;
            bool hasInlineValue = false;
            const size_t equals = arg.find ('=');

            if (arg.compare (0, 2, "--") == 0 && equals != std::string::npos)
            {
                inlineValue = arg.substr (equals + 1);
                arg.erase (equals);
                hasInlineValue = true;
            }

            if (arg == "--web-port" || arg == "--web-bind")
            {
                if (!hasInlineValue && i + 1 >= argc)
                {
                    Say (std::cerr) << "Error: " << arg << " needs a value\n";
                    return (1);
                }

                const char *text = hasInlineValue ? inlineValue.c_str () : argv[++i];

                if (arg == "--web-port")
                {
                    if (!parseWholeNumber (text, webPort) || webPort < 1 || webPort > 65535)
                    {
                        Say (std::cerr) << "Error: --web-port takes a port from 1 to 65535, not '" << text << "'\n";
                        return (1);
                    }
                }
                else
                {
                    webBind = text;
                }
            }
            else
            {
                Say (std::cerr) << "Error: unknown web option '" << argv[i] << "'\n\n";
                printUsage (argv[0]);
                return (1);
            }
        }

        std::string deviceIp = "192.168.3.53";

        {
            DeviceDiscovery discovery;

            if (discovery.loadFromFile ("devices.json"))
            {
                if (const SoundTouchDevice *device = discovery.getDefaultDevice ())
                {
                    deviceIp = device->ipAddress;
                }
            }
        }

        WebServer::Settings settings;

        settings.port = static_cast<int> (webPort);
        settings.bind = webBind;
        settings.version = WEB_VERSION;
        settings.relayPort = DEFAULT_RELAY_PORT;
        settings.relayBufferMb = static_cast<long> (StreamProxy::DEFAULT_BUFFER_MB);
        settings.titleOffsetSeconds = 0;
        settings.resume = true;
        settings.embedded = false;
        settings.defaultDeviceIp = deviceIp;

        WebServer server (settings);

        if (!server.start ())
        {
            return (1);
        }

        handleStopSignals ();

        Say () << "Web dashboard on http://" << (webBind.empty () ? "0.0.0.0" : webBind)
               << ":" << webPort << "  (Ctrl-C to stop)\n";

        while (!g_stopRequested)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (200));
        }

        server.stop ();

        Say () << "\nStopped.\n";

        return (0);
    }

    StreamConfig config;

    if (!config.loadFromFile ("streams.json"))
    {
        Say (std::cerr) << "Error: Could not load streams.json\n";
        Say (std::cerr) << "Make sure streams.json is in the current directory\n";
        return (1);
    }

    DeviceDiscovery discovery;

    const SoundTouchDevice *device = nullptr;
    std::string deviceIp = "192.168.3.53";

    if (discovery.loadFromFile ("devices.json"))
    {
        device = discovery.getDefaultDevice ();

        if (device != nullptr)
        {
            deviceIp = device->ipAddress;
        }
    }

    SoundTouchClient client (deviceIp);

    if (command == "list")
    {
        config.listStreams ();
        return (0);
    }

    if (command == "play")
    {
        if (argc < 3)
        {
            Say (std::cerr) << "Error: stream name required.\n\n";
            Say (std::cerr) << "Usage: " << argv[0] << " play <stream-name>\n";
            Say (std::cerr) << "Run '" << argv[0] << " list' to see available streams\n";
            return (1);
        }

        std::string streamName;
        bool withSong = false;

        for (int i = 2; i < argc; ++i)
        {
            const std::string arg (argv[i]);

            if (arg == "--song")
            {
                withSong = true;
            }
            else
            {
                streamName = arg;
            }
        }

        const Stream *stream = config.findByName (streamName);

        if (stream == nullptr)
        {
            Say (std::cerr) << "Error: Unknown stream '" << streamName << "'\n";
            Say (std::cerr) << "Run '" << argv[0] << " list' to see available streams\n";
            return (1);
        }

        // Off by default: the speaker cannot refresh this, so a song title goes stale as soon as
        // the song ends and then shows something untrue. The station name is always accurate.
        std::string song;
        std::string artist;

        if (withSong)
        {
            std::string title;

            IcyReader reader ([&title] (const std::string &seen)
            {
                title = seen;
                return (false);
            });

            reader.read (stream->url, 4);

            splitStreamTitle (title, artist, song);
        }

        return (client.playStream (stream->url, stream->displayName, song, artist) ? 0 : 1);
    }

    if (command == "stop")
    {
        return (client.stop () ? 0 : 1);
    }

    if (command == "nowplaying")
    {
        bool watch = false;
        bool push = false;
        int intervalSeconds = 0;         // 0 = hold the connection open
        std::string wanted;

        for (int i = 2; i < argc; ++i)
        {
            const std::string arg (argv[i]);

            if (arg == "--watch")
            {
                watch = true;
            }
            else if (arg == "--update-track-info")
            {
                push = true;
                watch = true;
            }
            else if (arg == "--interval" || arg.compare (0, 11, "--interval=") == 0)
            {
                const bool inlineValue = (arg != "--interval");

                if (!inlineValue && i + 1 >= argc)
                {
                    Say (std::cerr) << "Error: --interval needs a number of seconds\n";
                    return (1);
                }

                const char *text = inlineValue ? argv[i] + 11 : argv[++i];
                long seconds = 0;

                if (!parseWholeNumber (text, seconds) || seconds < 1 || seconds > 86400)
                {
                    Say (std::cerr) << "Error: --interval takes whole seconds from 1 to 86400, not '" << text << "'\n";
                    return (1);
                }

                intervalSeconds = static_cast<int> (seconds);
                watch = true;
            }
            else if (arg.compare (0, 2, "--") == 0)
            {
                Say (std::cerr) << "Error: unknown nowplaying option '" << arg << "'\n\n";
                printUsage (argv[0]);
                return (1);
            }
            else
            {
                wanted = arg;
            }
        }

        const Stream *stream = nullptr;

        if (!wanted.empty ())
        {
            stream = config.findByName (wanted);

            if (stream == nullptr)
            {
                Say (std::cerr) << "Error: Unknown stream '" << wanted << "'\n";
                Say (std::cerr) << "Run '" << argv[0] << " list' to see available streams\n";
                return (1);
            }
        }
        else
        {
            stream = findPlayingStream (config, client.currentStreamUrl ());

            if (stream == nullptr)
            {
                Say (std::cerr) << "Error: the speaker is not playing a stream from streams.json.\n";
                Say (std::cerr) << "Name one instead: " << argv[0] << " nowplaying klove\n";
                return (1);
            }
        }

        Say () << stream->displayName << (watch ? "  (Ctrl-C to stop)" : "") << "\n";

        if (intervalSeconds > 0)
        {
            Say () << "Sampling every " << intervalSeconds << "s.\n";
        }
        else if (watch)
        {
            Say () << "Holding the stream open; a change shows within about a second.\n";
        }

        if (push)
        {
            Say () << "Pushing each change to the speaker; expect a few seconds of "
                   << "silence per song.\n";
        }

        // Kept out here so it survives across samples, each of which is a fresh connection.
        std::string lastReported;

        auto onTitle = [&] (const std::string &title)
        {
            if (title == lastReported)
            {
                return (false);
            }

            lastReported = title;

            Say () << "  " << title << "\n";

            if (push)
            {
                const std::string playing = client.currentStreamUrl ();

                // Don't drag the speaker back if it has since been switched elsewhere, nor pull it
                // off control's relay, which updates the display itself.
                if (playing != stream->url)
                {
                    if (findPlayingStream (config, playing) == stream)
                    {
                        Say () << "    (control's relay is carrying " << stream->displayName
                               << " and updates the display itself; not updating)\n";
                    }
                    else
                    {
                        Say () << "    (speaker is no longer on " << stream->displayName
                               << "; not updating)\n";
                    }
                }
                else
                {
                    std::string artist;
                    std::string song;

                    splitStreamTitle (title, artist, song);

                    if (!client.updateTrackInfo (stream->url, stream->displayName, song, artist))
                    {
                        Say (std::cerr) << "    (failed to update the speaker)\n";
                    }
                }
            }

            // Sampling closes the connection after each look; holding open keeps reading.
            return (watch && intervalSeconds == 0);
        };

        // Watching runs until stopped, so Ctrl-C and docker stop end it cleanly, also as PID 1.
        if (watch)
        {
            handleStopSignals ();
        }

        auto stopping = [] () { return (g_stopRequested.load ()); };

        if (intervalSeconds == 0)
        {
            IcyReader reader (onTitle, stopping);

            // A stop can land while the reader is connecting, which then fails; that is still a stop.
            if (!reader.read (stream->url) && !g_stopRequested)
            {
                Say (std::cerr) << "Error reading stream metadata: " << reader.getError () << "\n";
                return (1);
            }

            return (0);
        }

        while (!g_stopRequested)
        {
            IcyReader reader (onTitle, stopping);

            if (!reader.read (stream->url) && !g_stopRequested)
            {
                Say (std::cerr) << "  (read failed: " << reader.getError () << "; retrying)\n";
            }

            for (int waited = 0; waited < intervalSeconds && !g_stopRequested; ++waited)
            {
                std::this_thread::sleep_for (std::chrono::seconds (1));
            }
        }

        return (0);
    }

    if (command == "status")
    {
        return (client.status () ? 0 : 1);
    }

    if (command == "presets")
    {
        return (client.presets () ? 0 : 1);
    }

    if (command == "save")
    {
        if (argc < 3)
        {
            Say (std::cerr) << "Error: preset number required.\n\n";
            Say (std::cerr) << "Example:\n";
            Say (std::cerr) << "  " << argv[0] << " save 3\n";
            return (1);
        }

        const int presetId = std::atoi (argv[2]);
        const Stream *stream = nullptr;

        if (argc >= 4)
        {
            const std::string streamName (argv[3]);

            stream = config.findByName (streamName);

            if (stream == nullptr)
            {
                Say (std::cerr) << "Error: Unknown stream '" << streamName << "'\n";
                Say (std::cerr) << "Run '" << argv[0] << " list' to see available streams\n";
                return (1);
            }
        }
        else
        {
            const std::string playingUrl = client.currentStreamUrl ();

            stream = findPlayingStream (config, playingUrl);

            if (stream == nullptr)
            {
                Say (std::cerr) << "Error: cannot tell which stream is playing.\n";

                if (playingUrl.empty ())
                {
                    Say (std::cerr) << "Play one first, or name it: " << argv[0] << " save "
                                    << presetId << " <stream-name>\n";
                }
                else
                {
                    Say (std::cerr) << "Playing " << playingUrl << ", which is not in streams.json\n";
                }

                return (1);
            }

            Say () << "Currently playing: " << stream->displayName << "\n";
        }

        Say () << "Saving " << stream->displayName << " to preset " << presetId << "...\n";

        if (!client.savePreset (presetId, stream->url, stream->displayName))
        {
            Say (std::cerr) << "Failed to store preset " << presetId << "\n";
            return (1);
        }

        return (client.presets () ? 0 : 1);
    }

    if (command == "program-presets")
    {
        const std::vector<const Stream *> presetStreams = config.getPresetStreams ();

        if (presetStreams.empty ())
        {
            Say (std::cerr) << "No streams in streams.json declare a \"preset\".\n";
            return (1);
        }

        const std::vector<int> buttons = config.getRequiredButtons ();

        Say () << "streams.json needs " << buttons.size () << " of the "
               << StreamConfig::MAX_BUTTON << " buttons.\n\n";

        int failed = 0;

        for (const int button : buttons)
        {
            const Stream *stream = config.findByPreset (button);
            bool placeholder = false;

            if (stream == nullptr)
            {
                // Used only inside combos. It still needs content or the speaker stays silent
                // about the press, so borrow a stream that the button takes part in.
                stream = config.findStreamUsingButton (button);
                placeholder = true;
            }

            if (stream == nullptr)
            {
                continue;
            }

            Say () << "  button " << button << " <- " << stream->displayName;

            if (placeholder)
            {
                Say () << " (placeholder: combo digit only)";
            }

            Say () << " ... ";

            if (client.savePreset (button, stream->url, stream->displayName))
            {
                Say () << "ok\n";
            }
            else
            {
                Say () << "FAILED\n";
                ++failed;
            }
        }

        Say () << "\n";

        for (const Stream *stream : presetStreams)
        {
            if (stream->preset > StreamConfig::MAX_BUTTON)
            {
                Say () << "  combo " << stream->preset << " -> " << stream->displayName
                       << " (press " << describeSequence (stream->preset)
                       << "; resolved by control mode, nothing stored)\n";
            }
        }

        Say () << "\n";

        if (failed != 0)
        {
            Say (std::cerr) << failed << " preset(s) could not be stored.\n";
            return (1);
        }

        Say () << "Presets on the speaker now:\n\n";

        return (client.presets () ? 0 : 1);
    }

    if (command == "select")
    {
        if (argc < 3)
        {
            Say (std::cerr) << "Error: preset required.\n\n";
            Say (std::cerr) << "Examples:\n";
            Say (std::cerr) << "  " << argv[0] << " select 3     # single button\n";
            Say (std::cerr) << "  " << argv[0] << " select 13    # combo: button 1 then 3\n";
            return (1);
        }

        const int preset = std::atoi (argv[2]);
        const Stream *stream = config.findByPreset (preset);

        if (stream == nullptr)
        {
            Say () << "Note: no stream is mapped to preset " << preset
                   << "; the speaker will stop and stay silent.\n";
        }
        else
        {
            Say () << "Preset " << preset << " is " << stream->displayName << "\n";
        }

        // Well inside the window so the digits read as one combo.
        const int gapMs = std::max (50, config.getComboWindowMs () / 3);

        if (!client.selectPreset (preset, gapMs))
        {
            return (1);
        }

        Say () << "\nThis only starts playback if 'control' is running to act on it.\n";

        return (0);
    }

    if (command == "control")
    {
        handleStopSignals ();

        // Writes to a socket the far end has closed then fail with EPIPE instead of killing the
        // process, and a log pipe that closes does not take control down with it.
        std::signal (SIGPIPE, SIG_IGN);

        bool pushTrackInfo = false;
        bool useProxy = true;
        bool autoResume = true;
        long relayPort = DEFAULT_RELAY_PORT;
        long bufferMb = StreamProxy::DEFAULT_BUFFER_MB;
        double titleOffsetSeconds = 0;
        bool webEnabled = false;
        long webPort = DEFAULT_WEB_PORT;
        std::string webBind;

        // Each setting can come from the environment, as in a container, and the command line
        // overrides it. An empty variable counts as not set.
        auto fromEnvironment = [] (const char *name) -> const char *
        {
            const char *value = std::getenv (name);

            return ((value != nullptr && *value != '\0') ? value : nullptr);
        };

        auto badPort = [] (const std::string &where, const char *text)
        {
            Say (std::cerr) << "Error: " << where << " takes a port from 1 to 65535, not '" << text << "'\n";
            return (1);
        };

        auto badBuffer = [] (const std::string &where, const char *text)
        {
            Say (std::cerr) << "Error: " << where << " takes whole megabytes from " << StreamProxy::MIN_BUFFER_MB
                            << " to " << StreamProxy::MAX_BUFFER_MB << ", not '" << text << "'\n";
            return (1);
        };

        auto badOffset = [] (const std::string &where, const char *text)
        {
            Say (std::cerr) << "Error: " << where << " takes seconds from -60 to 60, e.g. 2.5 or -1.5, not '" << text << "'\n";
            return (1);
        };

        auto validPort = [] (long port) { return (port >= 1 && port <= 65535); };

        auto validBuffer = [] (long megabytes)
        {
            return (megabytes >= static_cast<long> (StreamProxy::MIN_BUFFER_MB)
                    && megabytes <= static_cast<long> (StreamProxy::MAX_BUFFER_MB));
        };

        auto validOffset = [] (double seconds) { return (seconds >= -60 && seconds <= 60); };

        if (const char *text = fromEnvironment ("SOUNDTOUCH_RELAY_PORT"))
        {
            if (!parseWholeNumber (text, relayPort) || !validPort (relayPort))
            {
                return (badPort ("SOUNDTOUCH_RELAY_PORT", text));
            }
        }

        if (const char *text = fromEnvironment ("SOUNDTOUCH_RELAY_BUFFER"))
        {
            if (!parseWholeNumber (text, bufferMb) || !validBuffer (bufferMb))
            {
                return (badBuffer ("SOUNDTOUCH_RELAY_BUFFER", text));
            }
        }

        if (const char *text = fromEnvironment ("SOUNDTOUCH_TITLE_OFFSET"))
        {
            if (!parseSeconds (text, titleOffsetSeconds) || !validOffset (titleOffsetSeconds))
            {
                return (badOffset ("SOUNDTOUCH_TITLE_OFFSET", text));
            }
        }

        if (const char *text = fromEnvironment ("SOUNDTOUCH_WEB_PORT"))
        {
            if (!parseWholeNumber (text, webPort) || !validPort (webPort))
            {
                return (badPort ("SOUNDTOUCH_WEB_PORT", text));
            }

            webEnabled = true;
        }

        if (const char *text = fromEnvironment ("SOUNDTOUCH_WEB_BIND"))
        {
            webBind = text;
            webEnabled = true;
        }

        for (int i = 2; i < argc; ++i)
        {
            std::string arg (argv[i]);
            std::string inlineValue;
            bool hasInlineValue = false;

            // --name=value as well as --name value.
            const size_t equals = arg.find ('=');

            if (arg.compare (0, 2, "--") == 0 && equals != std::string::npos)
            {
                inlineValue = arg.substr (equals + 1);
                arg.erase (equals);
                hasInlineValue = true;
            }

            if (hasInlineValue && (arg == "--update-track-info" || arg == "--no-proxy" || arg == "--no-resume" || arg == "--web"))
            {
                Say (std::cerr) << "Error: " << arg << " takes no value\n";
                return (1);
            }

            if (arg == "--update-track-info")
            {
                pushTrackInfo = true;
            }
            else if (arg == "--no-proxy")
            {
                useProxy = false;
            }
            else if (arg == "--no-resume")
            {
                autoResume = false;
            }
            else if (arg == "--web")
            {
                webEnabled = true;
            }
            else if (arg == "--web-port" || arg == "--web-bind")
            {
                if (!hasInlineValue && i + 1 >= argc)
                {
                    Say (std::cerr) << "Error: " << arg << " needs a value\n";
                    return (1);
                }

                const char *text = hasInlineValue ? inlineValue.c_str () : argv[++i];

                webEnabled = true;

                if (arg == "--web-port" && (!parseWholeNumber (text, webPort) || !validPort (webPort)))
                {
                    return (badPort (arg, text));
                }

                if (arg == "--web-bind")
                {
                    webBind = text;
                }
            }
            else if (arg == "--title-offset" || arg == "--relay-port" || arg == "--relay-buffer")
            {
                if (!hasInlineValue && i + 1 >= argc)
                {
                    Say (std::cerr) << "Error: " << arg << " needs a value\n";
                    return (1);
                }

                const char *text = hasInlineValue ? inlineValue.c_str () : argv[++i];

                if (arg == "--title-offset" && (!parseSeconds (text, titleOffsetSeconds) || !validOffset (titleOffsetSeconds)))
                {
                    return (badOffset (arg, text));
                }

                if (arg == "--relay-port" && (!parseWholeNumber (text, relayPort) || !validPort (relayPort)))
                {
                    return (badPort (arg, text));
                }

                if (arg == "--relay-buffer" && (!parseWholeNumber (text, bufferMb) || !validBuffer (bufferMb)))
                {
                    return (badBuffer (arg, text));
                }
            }
            else
            {
                // A misspelt option would otherwise leave control running on defaults without a word.
                Say (std::cerr) << "Error: unknown control option '" << argv[i] << "'\n\n";
                printUsage (argv[0]);
                return (1);
            }
        }

        for (const Stream *stream : config.getPresetStreams ())
        {
            Say () << "Preset " << stream->preset;

            if (stream->preset > StreamConfig::MAX_BUTTON)
            {
                Say () << " (press " << describeSequence (stream->preset)
                       << ", up to " << config.getComboWindowMs () << "ms apart)";
            }

            Say () << " -> " << stream->displayName << "\n";
        }

        // The relay makes a song change cost ~1s of silence instead of ~4s, and knows when the
        // speaker actually reaches the next song. It is only worth running to push titles.
        StreamProxy proxy;

        if (pushTrackInfo && useProxy)
        {
            if (proxy.start (static_cast<int> (relayPort), deviceIp, static_cast<size_t> (bufferMb) * 1024 * 1024))
            {
                proxy.setTitleOffset (titleOffsetSeconds);

                // How long a pause can last and still carry on where it stopped, at the usual rate.
                Say () << "Relay listening on port " << relayPort << " for " << deviceIp << ", keeping "
                       << bufferMb << " MB of the stream (pauses of up to ~"
                       << StreamProxy::pauseMinutes (static_cast<size_t> (bufferMb) * 1024 * 1024, 8000)
                       << " min carry on where they stopped, at 64 kbps)\n";
            }
            else
            {
                Say () << "Relay could not start; falling back to direct streaming.\n";
            }
        }

        // The station list: as loaded at start, then as saved from the dashboard, which swaps in a new
        // one whole. Each use takes the list current at that moment, and a stream taken from it keeps
        // that version alive for as long as the stream is held (see holdStream).
        std::mutex streamsMutex;
        std::shared_ptr<const StreamConfig> liveStreams = std::make_shared<const StreamConfig> (config);

        auto currentStreams = [&] ()
        {
            std::lock_guard<std::mutex> lock (streamsMutex);

            return (liveStreams);
        };

        // What is playing now, shared by the event loop and the song watcher. When holding both,
        // take speakerMutex first.
        std::mutex speakerMutex;                 // serialises SetAVTransportURI callers
        std::mutex stationMutex;
        std::shared_ptr<const Stream> station;
        std::string speakerUrl;                  // what the speaker was actually given
        std::uint64_t generation = 0;            // the relay's id for this station; 0 when direct
        std::uint64_t handledOffset = 0;         // relay: titles up to here are dealt with
        std::string shownTitle;                  // the song on the display now
        std::uint64_t playEpoch = 0;             // counts plays, even of the station already on

        // For resuming: whether the speaker should be playing what control gave it, and what that
        // was, by its name, along with its preset. The first is cleared by anything the user does:
        // a press, standby, another source.
        std::atomic<bool> wantPlaying (false);
        std::mutex lastMutex;                    // lastStream, and saving it
        std::string lastStream;
        std::atomic<int> lastPreset (0);

        {
            int savedPreset = 0;
            std::string savedName;

            loadLastPlayed (savedPreset, savedName);

            const Stream *last = !savedName.empty () ? config.findByName (savedName) : config.findByPreset (savedPreset);

            if (last != nullptr)
            {
                lastStream = last->name;
                lastPreset = last->preset;
            }
        }

        // Notes what has just played as what to bring back, saving it when that changes.
        auto rememberPlayed = [&] (const Stream &stream)
        {
            std::lock_guard<std::mutex> lock (lastMutex);

            if (lastStream == stream.name && lastPreset == stream.preset)
            {
                return;
            }

            lastStream = stream.name;
            lastPreset = stream.preset;

            saveLastPlayed (stream.preset, stream.name);
        };

        // What to bring back, as the list has it now; null when nothing has played, or when what
        // played is no longer in the list.
        auto lastPlayed = [&] () -> std::shared_ptr<const Stream>
        {
            const std::shared_ptr<const StreamConfig> streams = currentStreams ();
            std::lock_guard<std::mutex> lock (lastMutex);

            return (lastStream.empty () ? nullptr : holdStream (streams, streams->findByName (lastStream)));
        };

        // control's stream at this location: one in the list, or the one control itself gave the
        // speaker there, even if it has been renamed or taken out of the list since. Null for
        // anything else, such as another app's stream.
        auto streamAt = [&] (const std::string &location) -> std::shared_ptr<const Stream>
        {
            const std::shared_ptr<const StreamConfig> streams = currentStreams ();

            if (const Stream *listed = findPlayingStream (*streams, location))
            {
                return (holdStream (streams, listed));
            }

            std::lock_guard<std::mutex> lock (stationMutex);

            return ((station != nullptr && !location.empty () && location == speakerUrl) ? station : nullptr);
        };

        std::atomic<long long> lastPushMs (0);
        std::atomic<long long> lastPlayMs (0);

        // The speaker's volume, kept current from its /volume event stream (and read once at start).
        // -1 until it is first known, so the web layer can tell "not read yet" from a real level.
        std::atomic<int> currentVolume (-1);
        std::atomic<int> currentVolumeTarget (-1);
        std::atomic<bool> currentMuted (false);

        // What the speaker last said it is doing: its source (UPNP, STANDBY, BLUETOOTH, ...), play
        // status and the stream it is on, from its event stream and a look at start. The dashboard
        // shows it, and enables its play/pause and power buttons by it. Empty until first known.
        std::mutex speakerStateMutex;
        std::string speakerSource;
        std::string speakerStatus;
        std::string speakerLocation;

        // Raised when the volume or the speaker's state above changes, so the dashboard's held request
        // answers at once. Declared before the dashboard, which waits on it, so it outlives it.
        LiveSignal liveSignal;
        std::atomic<unsigned> pressCount (0);        // lets a resume see a press that came after it
        std::atomic<bool> pausedByUser (false);
        std::deque<std::chrono::steady_clock::time_point> resumeTimes;    // play thread only

        // A pause that came just after one of control's own commands, until it is clear whose it
        // was (steady milliseconds; 0 for none). Until then it counts as a pause for anything that
        // would play, and nothing is pushed.
        std::atomic<long long> pendingPauseMs (0);

        // How many connections the speaker had opened when it was paused: one more means it was
        // played again, even if the event saying so was missed.
        std::atomic<std::uint64_t> pausedConnections (0);

        // The remote's skip buttons: songs on (+) or back (-) still to be taken, and whether the
        // speaker is on one of control's streams, the only ones control skips.
        std::atomic<int> skipSteps (0);
        std::atomic<bool> onControlStream (false);
        std::atomic<unsigned> skipPresses (0);       // lets a play see a skip press that came after it
        std::atomic<bool> skipPending (false);       // a press not yet acted on, even if they cancel out
        std::atomic<long long> lastBackMs (0);       // when previous last took the speaker back
        std::atomic<long long> lastPressMs (0);      // a preset or skip press, which is about to play
        std::atomic<long long> pausedSinceMs (0);    // when the pause control is holding began

        std::atomic<bool> watcherStop (false);

        // Paused or stopped from the remote or another app, at the given moment: leave it so. Hold
        // the relay's idea of where it is from that moment, so nothing is pushed to it, nothing
        // resumes it, and the remote's Play carries on from there.
        auto confirmPause = [&] (long long atMs)
        {
            wantPlaying = false;
            pausedByUser = true;
            pausedSinceMs = steadyMilliseconds ();

            if (proxy.isRunning ())
            {
                proxy.noteSpeakerStopped (steadyTimeFromMilliseconds (atMs));
                pausedConnections = proxy.speakerConnections ();
            }
        };

        // A pause left pending is the user's once the speaker has not gone on to buffer or play by
        // the end of the window.
        auto settlePause = [&] ()
        {
            long long since = pendingPauseMs;

            if (since == 0)
            {
                return;
            }

            const long long now = steadyMilliseconds ();
            const long long windowEnd = std::max (lastPushMs.load (), lastPlayMs.load ())
                + std::chrono::duration_cast<std::chrono::milliseconds> (OWN_TRANSITION_WINDOW).count ();
            const long long confirmAt = since + std::chrono::duration_cast<std::chrono::milliseconds> (PAUSE_CONFIRM).count ();

            if (now >= std::max (windowEnd, confirmAt) && pendingPauseMs.compare_exchange_strong (since, 0))
            {
                if (wantPlaying)
                {
                    confirmPause (since);
                }
            }
        };

        // Played again from the remote: carry on from where it was held.
        auto resumeFromPause = [&] ()
        {
            pendingPauseMs = 0;

            if (pausedByUser.exchange (false))
            {
                wantPlaying = true;

                if (proxy.isRunning ())
                {
                    proxy.noteSpeakerResumed ();
                }

                Say () << ">>> Played again after a pause.\n";
            }
        };

        // The speaker is paused or stopped on one of control's streams, as after control was
        // restarted: take it over as it is and leave it so, never playing it. Then the remote's Play
        // has the relay to come back to, which starts it on the song now playing.
        auto adoptPaused = [&] (const std::shared_ptr<const Stream> &on, const std::string &location, const std::string &status)
        {
            pendingPauseMs = 0;
            wantPlaying = false;
            pausedByUser = true;
            pausedSinceMs = steadyMilliseconds ();
            onControlStream = true;

            std::lock_guard<std::mutex> speakerLock (speakerMutex);

            {
                std::lock_guard<std::mutex> lock (stationMutex);

                // Already known: only the flags were out of date, say after an event was missed.
                if (sameStream (station, on) && speakerUrl == location)
                {
                    if (proxy.isRunning ())
                    {
                        proxy.noteSpeakerStopped ();
                        pausedConnections = proxy.speakerConnections ();
                    }

                    return;
                }
            }

            // Only a relay URL on this relay's port and address comes back here.
            std::uint64_t relayGeneration = 0;

            if (proxy.isRunning () && location == proxy.urlForSpeaker (on->name))
            {
                relayGeneration = proxy.setUpstream (on->url);
            }

            {
                std::lock_guard<std::mutex> lock (stationMutex);

                station = on;
                speakerUrl = location;
                generation = relayGeneration;
                handledOffset = 0;
                shownTitle.clear ();
                ++playEpoch;
            }

            if (proxy.isRunning ())
            {
                pausedConnections = proxy.speakerConnections ();
            }

            rememberPlayed (*on);

            const bool relayed = !StreamProxy::streamNameFromUrl (location, 0).empty ();

            Say () << ">>> The speaker is " << (status == "STOP_STATE" ? "stopped" : "paused") << " on "
                   << on->displayName << "; leaving it so."
                   << ((relayGeneration != 0) ? " Play on the remote starts the song now playing.\n"
                          : relayed ? " It was given another relay's address, so press a preset to play it.\n"
                          : "\n");
        };

        auto playCallback = [&] (int presetId, WebSocketListener::PlayReason reason, const std::string &streamName)
        {
            using PlayReason = WebSocketListener::PlayReason;

            // A press is for a preset, or from the dashboard for a stream by its name. Anything else
            // but a skip brings back what played last, if anything did: a look at the speaker may
            // come with nothing to bring back.
            std::shared_ptr<const Stream> stream;

            if (reason == PlayReason::PRESS)
            {
                const std::shared_ptr<const StreamConfig> streams = currentStreams ();

                stream = holdStream (streams, streamName.empty () ? streams->findByPreset (presetId) : streams->findByName (streamName));

                if (stream == nullptr)
                {
                    return (false);
                }
            }
            else if (reason != PlayReason::SKIP)
            {
                stream = lastPlayed ();
            }

            // A skip is within whatever is playing, preset or not, by however many presses came.
            int steps = 0;

            if (reason == PlayReason::SKIP)
            {
                // Each press dropped the stream, so even presses that cancel out need it played again.
                if (!skipPending.exchange (false))
                {
                    return (true);              // an earlier skip already took these presses
                }

                steps = skipSteps.exchange (0);

                std::lock_guard<std::mutex> lock (stationMutex);

                stream = station;

                if (stream == nullptr)
                {
                    return (true);
                }
            }

            const unsigned pressesAtStart = pressCount;

            // Read after the skip's steps are taken, so a press that came in between is this play's.
            const unsigned skipsAtStart = skipPresses;

            // A press means someone is there: the limit on automatic resumes starts over.
            if (reason == PlayReason::PRESS || reason == PlayReason::SKIP)
            {
                resumeTimes.clear ();
            }

            if (reason != PlayReason::PRESS && reason != PlayReason::SKIP)
            {
                // Never play over a pause, even one still settling: only look.
                settlePause ();

                if (pausedByUser || pendingPauseMs != 0)
                {
                    reason = PlayReason::RESYNC;
                }

                const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ();

                while (!resumeTimes.empty () && now - resumeTimes.front () > RESUME_LIMIT_WINDOW)
                {
                    resumeTimes.pop_front ();
                }

                if (reason == PlayReason::RESUME_DROP && resumeTimes.size () >= RESUME_LIMIT)
                {
                    Say () << ">>> The speaker keeps stopping by itself; no more automatic "
                           << "resumes until a preset is pressed.\n";
                    wantPlaying = false;
                    return (true);
                }

                // Back from a reboot, the event stream can come up before the rest of the speaker.
                SoundTouchClient::NowPlaying speakerNow = client.nowPlaying ();

                for (std::chrono::steady_clock::time_point giveUp = now + SPEAKER_READY_WAIT;
                     speakerNow.source.empty () && std::chrono::steady_clock::now () < giveUp && !g_stopRequested
                     && pressCount == pressesAtStart; speakerNow = client.nowPlaying ())
                {
                    std::this_thread::sleep_for (std::chrono::seconds (2));
                }

                if (pressCount != pressesAtStart || g_stopRequested)
                {
                    return (true);              // a press has it now
                }

                const std::string &source = speakerNow.source;

                // Never switch it on from standby at start-up, nor take it over from another
                // source, UPnP included: another app may be streaming to it.
                if (source == "STANDBY" && reason == PlayReason::RESUME_START)
                {
                    if (stream != nullptr && autoResume)
                    {
                        Say () << ">>> The speaker is off; not resuming " << stream->displayName
                               << ". Press a preset to play.\n";
                    }

                    return (true);
                }

                if (!source.empty () && source != "STANDBY" && source != "UPNP" && source != "INVALID_SOURCE")
                {
                    if (stream != nullptr && reason != PlayReason::RESYNC)
                    {
                        Say () << ">>> The speaker is on " << source << "; not resuming "
                               << stream->displayName << ".\n";
                    }

                    wantPlaying = false;
                    return (true);
                }

                if (source == "UPNP" && isStreamLocation (speakerNow.location))
                {
                    const std::shared_ptr<const Stream> on = streamAt (speakerNow.location);

                    // Playing, paused or stopped, a stream that is not control's belongs to another app.
                    if (on == nullptr)
                    {
                        if (stream != nullptr && reason != PlayReason::RESYNC)
                        {
                            Say () << ">>> The speaker is on something else over UPnP; not resuming "
                                   << stream->displayName << ".\n";
                        }

                        wantPlaying = false;
                        return (true);
                    }

                    // Already paused by the user, either counts. Otherwise only a pause that lasts is
                    // theirs: losing its stream under it, as when control was just restarted, the
                    // speaker can pass through a pause, and a stop means the stream ended. Both of
                    // those are brought back below.
                    if (speakerNow.status == "PAUSE_STATE" || speakerNow.status == "STOP_STATE")
                    {
                        bool paused = (reason == PlayReason::RESYNC);

                        if (!paused && speakerNow.status == "PAUSE_STATE")
                        {
                            std::this_thread::sleep_for (PAUSE_SETTLE);

                            if (pressCount != pressesAtStart || g_stopRequested)
                            {
                                return (true);
                            }

                            const SoundTouchClient::NowPlaying again = client.nowPlaying ();

                            paused = (again.source == "UPNP" && again.location == speakerNow.location
                                      && again.status == "PAUSE_STATE");
                        }

                        if (paused)
                        {
                            adoptPaused (on, speakerNow.location, speakerNow.status);
                            return (true);
                        }
                    }

                    std::string given;

                    {
                        std::lock_guard<std::mutex> lock (stationMutex);

                        given = speakerUrl;
                    }

                    // Already back on what control gave it, say after recovering by itself, or
                    // played again while the event saying so was missed.
                    if ((speakerNow.status == "PLAY_STATE" || speakerNow.status == "BUFFERING_STATE")
                        && speakerNow.location == given)
                    {
                        if (pausedByUser || pendingPauseMs != 0)
                        {
                            resumeFromPause ();
                        }

                        wantPlaying = true;
                        onControlStream = true;
                        return (true);
                    }
                }

                // Only looking: whatever it is doing now is the user's doing.
                if (reason == PlayReason::RESYNC || (reason == PlayReason::RESUME_START && !autoResume)
                    || stream == nullptr)
                {
                    return (true);
                }

                // Counted only when a play is really going to be sent.
                if (reason == PlayReason::RESUME_DROP)
                {
                    resumeTimes.push_back (now);
                }

                Say () << "\n>>> Resuming " << stream->displayName
                       << (reason == PlayReason::RESUME_START ? " from last time" : "")
                       << ".\n";
            }

            // Held throughout, so the watcher cannot slip a song update in mid-switch.
            std::lock_guard<std::mutex> speakerLock (speakerMutex);

            std::string url = stream->url;
            std::uint64_t relayGeneration = 0;

            // What the display shows now, in case this turns out to be a re-press.
            std::uint64_t previousGeneration = 0;
            std::uint64_t previousHandled = 0;
            std::string previousTitle;

            {
                std::lock_guard<std::mutex> lock (stationMutex);

                previousGeneration = generation;
                previousHandled = handledOffset;
                previousTitle = shownTitle;

                if (proxy.isRunning ())
                {
                    relayGeneration = proxy.setUpstream (stream->url);
                    url = proxy.urlForSpeaker (stream->name);
                }

                station = stream;
                speakerUrl = url;
                generation = relayGeneration;
                handledOffset = 0;
                shownTitle.clear ();
                ++playEpoch;
            }

            // Waiting for the station's first title, and the rest of its opening backlog, lets this
            // command carry the right song. Otherwise it would follow moments later as a second
            // command, and cost a second gap.
            std::string artist;
            std::string song;
            StreamProxy::TitleMark first;
            std::uint64_t startOffset = 0;
            bool resumes = false;
            StreamProxy::Skip skip {};
            const bool again = (steps < 0 && steadyMilliseconds () - lastBackMs
                                < std::chrono::duration_cast<std::chrono::milliseconds> (PREVIOUS_AGAIN).count ());
            const bool skipped = (reason == PlayReason::SKIP && relayGeneration != 0
                                  && proxy.skipFrom (relayGeneration, steps, again, skip));

            if (skipped)
            {
                std::lock_guard<std::mutex> lock (stationMutex);

                // Carrying on just after an early push (a negative --title-offset), the display is
                // already on the coming song: keep it, as a re-press does.
                if (skip.songs == 0 && steps >= 0 && !skip.catchUp && relayGeneration == previousGeneration
                    && previousHandled > skip.mark.offset && !previousTitle.empty ())
                {
                    skip.mark = StreamProxy::TitleMark { previousHandled, previousTitle };
                }

                const std::string title = skip.mark.title.empty () ? std::string ("this song") : skip.mark.title;
                const char *partly = skip.partial ? " (as far back as the relay has it)" : "";
                Say line;

                if (skip.catchUp)
                {
                    line << ">>> " << (steps > 0 ? "Next" : steps < 0 ? "Previous" : "Skip")
                         << ": the speaker is further behind than the relay holds; skipping ahead to the song now playing: "
                         << title << "\n";
                }
                else if (steps == 0)
                {
                    line << ">>> Next and previous cancelled out; carrying on where it was\n";
                }
                else if (steps > 0)
                {
                    line << ">>> Next: ";

                    if (skip.songs == 0)
                    {
                        line << "no newer song has arrived yet; carrying on where it was\n";
                    }
                    else
                    {
                        line << "skipping ahead " << skip.songs << (skip.songs == 1 ? " song" : " songs") << " to " << title << "\n";
                    }
                }
                else if (skip.songs == 0)
                {
                    line << ">>> Previous: back to the start of " << title << partly << "\n";
                }
                else
                {
                    line << ">>> Previous: back " << skip.songs << (skip.songs == 1 ? " song" : " songs") << " to "
                         << title << partly << "\n";
                }

                // Carrying on, the display already shows the right song.
                handledOffset = skip.mark.offset;
                shownTitle = skip.mark.title;

                proxy.resumeAt (relayGeneration, skip.start);

                splitStreamTitle (skip.mark.title, artist, song);
            }
            else if (reason == PlayReason::SKIP)
            {
                Say () << ">>> " << (steps > 0 ? "Next" : steps < 0 ? "Previous" : "Skip")
                       << (relayGeneration == 0 ? " needs the relay; starting " : ": cannot tell where the speaker was; starting ")
                       << stream->displayName << " again\n";
            }

            if (!skipped && relayGeneration != 0
                && proxy.waitForTitle (relayGeneration, FIRST_TITLE_WAIT, first, startOffset, resumes))
            {
                std::lock_guard<std::mutex> lock (stationMutex);

                // A re-press carries on where the speaker stopped. If the title in force there is
                // one the watcher has already dealt with, the display shows the right song, with
                // brief titles weeded out, so keep that. A title it has not dealt with, as during
                // a retry, is the real news, unless it is itself brief: the speaker may have
                // stopped on a station ID before the watcher got to it.
                if (resumes && relayGeneration == previousGeneration && !previousTitle.empty ())
                {
                    bool keep = (first.offset <= previousHandled);

                    if (!keep)
                    {
                        const StreamProxy::Timeline timeline = proxy.getTimeline (relayGeneration, previousHandled);

                        if (timeline.bytesPerSecond > 0 && isBriefTitle (timeline, first))
                        {
                            // If a real song came between, as when its pushes kept failing, that
                            // is what is playing under the ID; otherwise the display is right.
                            const StreamProxy::TitleMark *song = nullptr;

                            for (const StreamProxy::TitleMark &earlier : timeline.upcoming)
                            {
                                if (earlier.offset >= first.offset)
                                {
                                    break;
                                }

                                if (!isBriefTitle (timeline, earlier))
                                {
                                    song = &earlier;
                                }
                            }

                            if (song != nullptr)
                            {
                                first = *song;
                            }
                            else
                            {
                                keep = true;
                            }
                        }
                    }

                    if (keep)
                    {
                        first.offset = previousHandled;
                        first.title = previousTitle;
                    }
                }

                handledOffset = first.offset;
                shownTitle = first.title;

                // Begin on a song that started inside the backlog, or where a re-press left off.
                if (startOffset != 0)
                {
                    proxy.resumeAt (relayGeneration, startOffset);
                }

                splitStreamTitle (first.title, artist, song);
            }

            if (g_stopRequested)
            {
                return (true);
            }

            if (reason != PlayReason::PRESS && pressCount != pressesAtStart)
            {
                Say () << ">>> A button was pressed meanwhile; leaving it to that.\n";
                return (true);
            }

            lastPlayMs = steadyMilliseconds ();

            // A press that comes while the speaker is starting this stream takes over from it, a skip
            // press included, and a stop does not wait for it, nor try it again.
            auto superseded = [&] ()
            {
                return (pressCount != pressesAtStart || skipPresses != skipsAtStart || g_stopRequested);
            };

            // Sent once more, it has to start in the same place.
            const std::uint64_t startFrom = skipped ? skip.start : startOffset;

            auto beforeRetry = [&] ()
            {
                // The Stop that comes next is this play's own, not the user's.
                lastPlayMs = steadyMilliseconds ();

                if (relayGeneration != 0 && startFrom != 0)
                {
                    proxy.resumeAt (relayGeneration, startFrom);
                }
            };

            const bool played = client.playStream (url, stream->displayName, song, artist, superseded, beforeRetry);

            if (!played && superseded ())
            {
                // A skip press says for itself what happens next.
                if (!g_stopRequested && pressCount != pressesAtStart)
                {
                    Say () << ">>> A button was pressed while " << stream->displayName << " was starting; "
                           << "leaving it to that.\n";
                }

                return (true);
            }

            if (played)
            {
                pendingPauseMs = 0;
                wantPlaying = true;
                onControlStream = true;

                // Whatever held the speaker as paused, it is playing what this sent now.
                if (pausedByUser.exchange (false) && proxy.isRunning ())
                {
                    proxy.noteSpeakerResumed ();
                }

                if (reason == PlayReason::SKIP)
                {
                    lastBackMs = (steps < 0) ? steadyMilliseconds () : 0;
                }

                // A skip stays on what was playing, which may have no preset of its own.
                rememberPlayed (*stream);
            }

            return (played);
        };

        auto presetInfoCallback = [&] (int presetId, std::string &name)
        {
            const std::shared_ptr<const StreamConfig> streams = currentStreams ();
            const Stream *stream = streams->findByPreset (presetId);

            if (stream == nullptr)
            {
                return (false);
            }

            name = stream->displayName;

            return (true);
        };

        auto couldExtendCallback = [&] (int sequence)
        {
            return (currentStreams ()->hasLongerPresetStartingWith (sequence));
        };

        // Relay: push each title when the speaker reaches it, and have the speaker resume exactly
        // there, so the silence falls between the songs and nothing is skipped.
        auto watchRelay = [&] ()
        {
            std::chrono::steady_clock::time_point retryAt {};
            std::uint64_t failedOffset = 0;     // a title whose push failed and is being retried

            // --title-offset is decided once per title: whether this one can be moved as asked.
            std::uint64_t decidedFor = std::numeric_limits<std::uint64_t>::max ();
            bool shiftIgnored = false;

            // Since when the speaker has been waiting to be started again, and when it last was.
            std::chrono::steady_clock::time_point restartSeen {};
            std::chrono::steady_clock::time_point restartSent {};

            // While paused, a speaker asking for the stream is not proof it was played again: it
            // also asks when a connection it held open through the pause is dropped. Only it can
            // say, so it is asked, no more often than this.
            std::chrono::steady_clock::time_point nextLook {};

            // A quick look, taken often: a speaker that does not answer must not hold up a stop or
            // fill the log.
            auto speakerSaysPlaying = [&] (const std::string &url)
            {
                const SoundTouchClient::NowPlaying now = client.glance (1500);

                return (now.source == "UPNP" && now.location == url
                        && (now.status == "PLAY_STATE" || now.status == "BUFFERING_STATE"));
            };

            while (!watcherStop)
            {
                std::this_thread::sleep_for (WATCH_TICK);

                if (std::chrono::steady_clock::now () < retryAt)
                {
                    continue;
                }

                std::shared_ptr<const Stream> mine;
                std::string target;
                std::uint64_t myGeneration = 0;
                std::uint64_t after = 0;

                {
                    std::lock_guard<std::mutex> lock (stationMutex);

                    mine = station;
                    target = speakerUrl;
                    myGeneration = generation;
                    after = handledOffset;
                }

                if (mine == nullptr || myGeneration == 0)
                {
                    continue;
                }

                const StreamProxy::Timeline timeline = proxy.getTimeline (myGeneration, after);

                settlePause ();

                if (timeline.generation != myGeneration)
                {
                    continue;
                }

                // It asked to carry on from where it paused, which the relay no longer holds, as
                // after a long pause or a restart of control: start it again on the song now playing,
                // from its beginning, once it is playing. Until then its request waits unanswered.
                if (timeline.speakerNeedsRestart)
                {
                    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ();

                    if (pausedByUser || pendingPauseMs != 0)
                    {
                        if (now < nextLook)
                        {
                            continue;
                        }

                        nextLook = now + std::chrono::seconds (1);

                        if (!speakerSaysPlaying (target))
                        {
                            continue;
                        }

                        resumeFromPause ();
                    }

                    // Asking again before the last start has reached it, or before a play or push control
                    // has just sent, or a press is about to: that opens a new connection of its own,
                    // which answers the request.
                    if (now - restartSent < RESTART_SETTLE
                        || steadyMilliseconds () - std::max ({ lastPlayMs.load (), lastPushMs.load (), lastPressMs.load () })
                           < std::chrono::duration_cast<std::chrono::milliseconds> (RESTART_SETTLE).count ())
                    {
                        continue;
                    }

                    if (restartSeen == std::chrono::steady_clock::time_point {})
                    {
                        restartSeen = now;
                    }

                    // A station only just connected to needs a moment for its rate and first title; one
                    // that sends no titles is started without one.
                    if ((timeline.bytesPerSecond == 0 || (timeline.catchUp.mark.title.empty () && timeline.titlesOffered))
                        && now - restartSeen < FIRST_TITLE_WAIT)
                    {
                        continue;
                    }

                    restartSeen = {};

                    std::lock_guard<std::mutex> speakerLock (speakerMutex);
                    std::lock_guard<std::mutex> stationLock (stationMutex);

                    if (generation != myGeneration || handledOffset != after)
                    {
                        continue;
                    }

                    const StreamProxy::CatchUp &restart = timeline.catchUp;
                    const std::string previousTitle = shownTitle;

                    handledOffset = restart.mark.offset;
                    shownTitle = restart.mark.title;

                    std::string artist;
                    std::string song;

                    splitStreamTitle (restart.mark.title, artist, song);

                    Say () << ">>> starting " << mine->displayName << " again on the song now playing"
                           << (restart.mark.title.empty () ? std::string () : ": " + restart.mark.title) << "\n";

                    proxy.resumeAt (myGeneration, restart.start);
                    lastPushMs = steadyMilliseconds ();
                    restartSent = now;

                    if (!client.updateTrackInfo (target, mine->displayName, song, artist))
                    {
                        // It asks again every few seconds, which brings this round once more.
                        proxy.cancelResume ();

                        shownTitle = previousTitle;
                        handledOffset = after;
                        retryAt = now + PUSH_RETRY;

                        Say (std::cerr) << ">>> could not start it again; retrying\n";
                    }

                    continue;
                }

                restartSeen = {};

                // Paused on a connection it kept open, a Play shows as nothing but an event, which the
                // event stream may be down for: ask it now and then. Not at once: its status can lag
                // the pause event.
                if (pausedByUser && timeline.speakerAttached && timeline.speakerConnections == pausedConnections
                    && steadyMilliseconds () - pausedSinceMs > 2000)
                {
                    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ();

                    if (now >= nextLook)
                    {
                        nextLook = now + std::chrono::seconds (1);

                        if (speakerSaysPlaying (target))
                        {
                            resumeFromPause ();
                        }
                    }
                }

                // A new connection while paused: played again without the event saying so, say while
                // the event stream was down, or reconnected while still paused, as when a connection
                // it held open was dropped. Ask it; still paused, hold its place from where the new
                // connection began.
                if (pausedByUser && timeline.speakerAttached && timeline.speakerConnections != pausedConnections)
                {
                    pausedConnections = timeline.speakerConnections;

                    if (speakerSaysPlaying (target))
                    {
                        resumeFromPause ();
                    }
                    else
                    {
                        proxy.noteSpeakerStopped (timeline.speakerConnectedAt);
                    }
                }

                // Without a byte rate there is no telling where the speaker is, so wait for one.
                // Nothing goes to a speaker paused from the remote, whatever its connections do, nor
                // while a pause is still settling.
                if (pausedByUser || pendingPauseMs != 0 || !timeline.speakerAttached
                    || timeline.bytesPerSecond == 0 || timeline.upcoming.empty ())
                {
                    continue;
                }

                const StreamProxy::TitleMark &mark = timeline.upcoming.front ();

                // Earlier only works as far as the relay sees ahead of the speaker, judged when the
                // title first shows up. Asked for more than that, the push stays at the title change.
                if (mark.offset != decidedFor)
                {
                    decidedFor = mark.offset;
                    shiftIgnored = !earlierFits (titleOffsetSeconds, timeline.bytesPerSecond, mark.offset,
                                                 timeline.speakerPosition);

                    if (shiftIgnored)
                    {
                        const double ahead = (mark.offset > timeline.speakerPosition)
                            ? static_cast<double> (mark.offset - timeline.speakerPosition) / timeline.bytesPerSecond : 0.0;

                        Say () << ">>> title offset " << titleOffsetSeconds << "s is more than the "
                               << tenths (ahead) << "s the relay can see ahead; "
                               << "pushing at the title change: " << mark.title << "\n";
                    }
                }

                // The station's title change, moved by --title-offset when that is possible.
                std::uint64_t pushAt = pushPoint (mark.offset, shiftIgnored ? 0.0 : titleOffsetSeconds,
                                                  timeline.bytesPerSecond);

                // Later never into the next song: that would put this title over the next one. Push
                // now instead, from where the speaker is.
                if (!shiftIgnored && titleOffsetSeconds > 0 && timeline.upcoming.size () > 1
                    && timeline.upcoming[1].offset <= pushAt)
                {
                    shiftIgnored = true;
                    pushAt = mark.offset;

                    Say () << ">>> title offset " << titleOffsetSeconds << "s would reach the next song; "
                           << "pushing now: " << mark.title << "\n";
                }

                if (timeline.speakerPosition < pushAt)
                {
                    continue;                   // not there yet
                }

                // Judged on what has arrived by the time the speaker gets here, so a push is never
                // held back past that. The speaker normally trails by ~10s, which is enough; just
                // after starting on a song inside the backlog it trails by less for a few songs,
                // and the window is that much shorter.
                const std::uint64_t rate = static_cast<std::uint64_t> (timeline.bytesPerSecond);
                const bool fleeting = isBriefTitle (timeline, mark);

                // The speaker carries on from the push point, so moving the push only moves where the
                // silence falls, never repeats or skips anything. A retry after a failed push, or a
                // push brought forward because the offset could not be kept, finds the speaker well
                // past that point and carries on from where it is rather than replay what it played.
                const bool retrying = (mark.offset == failedOffset);
                const std::uint64_t resumeOffset = ((retrying || shiftIgnored) && timeline.speakerPosition > pushAt + rate)
                                                 ? timeline.speakerPosition : std::min (pushAt, timeline.written);

                // Further behind than the relay holds, after long pauses or many song changes: rather
                // than start part way into this song, skip ahead to the song now playing, from its
                // beginning.
                const bool catchingUp = (resumeOffset < timeline.resumeFloor);
                const StreamProxy::TitleMark &pushed = catchingUp ? timeline.catchUp.mark : mark;
                const std::uint64_t startAt = catchingUp ? timeline.catchUp.start : resumeOffset;

                std::lock_guard<std::mutex> speakerLock (speakerMutex);
                std::lock_guard<std::mutex> stationLock (stationMutex);

                // A new play may have started while this was being decided.
                if (generation != myGeneration || handledOffset != after)
                {
                    continue;
                }

                handledOffset = catchingUp ? std::max (mark.offset, pushed.offset) : mark.offset;

                if (fleeting && !catchingUp)
                {
                    Say () << ">>> skipping brief title: " << mark.title << "\n";
                    continue;
                }

                if (pushed.title == shownTitle && !catchingUp)
                {
                    continue;
                }

                // A speaker a press has stopped gets nothing until it is played again. Brief and
                // unchanged titles it has reached are still settled above, so a re-press finds
                // them dealt with; this one is left for when it plays.
                if (timeline.speakerStopped)
                {
                    handledOffset = after;
                    continue;
                }

                const std::string previousTitle = shownTitle;

                shownTitle = pushed.title;

                std::string artist;
                std::string song;

                splitStreamTitle (pushed.title, artist, song);

                if (catchingUp)
                {
                    Say () << ">>> the speaker is " << tenths (static_cast<double> (timeline.written - timeline.speakerPosition) / timeline.bytesPerSecond)
                           << "s behind the station, more than the relay holds; skipping ahead to the song now playing\n";
                }

                Say () << ">>> now playing: " << pushed.title << "\n";

                proxy.resumeAt (myGeneration, startAt);
                lastPushMs = steadyMilliseconds ();

                if (client.updateTrackInfo (target, mine->displayName, song, artist))
                {
                    failedOffset = 0;
                }
                else
                {
                    // Try again shortly rather than leave the old song up for the whole track.
                    proxy.cancelResume ();

                    shownTitle = previousTitle;
                    handledOffset = after;
                    failedOffset = mark.offset;
                    retryAt = std::chrono::steady_clock::now () + PUSH_RETRY;

                    Say (std::cerr) << ">>> could not update the display; retrying in "
                                    << PUSH_RETRY.count () << "s\n";
                }
            }
        };

        // Direct: one long-held connection of our own. The speaker trails it by the CDN's opening
        // backlog, so titles land early.
        auto watchDirect = [&] ()
        {
            while (!watcherStop)
            {
                std::shared_ptr<const Stream> mine;
                std::string target;
                std::uint64_t myEpoch = 0;

                {
                    std::lock_guard<std::mutex> lock (stationMutex);

                    mine = station;
                    target = speakerUrl;
                    myEpoch = playEpoch;
                }

                if (mine == nullptr)
                {
                    std::this_thread::sleep_for (std::chrono::milliseconds (400));
                    continue;
                }

                // The newest title not yet on the display, and since when it has been newest.
                // Only this thread touches these: the reader calls back on it.
                std::string pending;
                std::chrono::steady_clock::time_point pendingSince {};
                std::chrono::steady_clock::time_point retryAt {};
                bool saidAway = false;

                auto pushSettled = [&] ()
                {
                    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ();

                    // Titles arrive early here already, so only a later push can be asked for.
                    const std::chrono::milliseconds extra (static_cast<long long> (std::max (0.0, titleOffsetSeconds) * 1000));

                    if (pending.empty () || now - pendingSince < DIRECT_SETTLE + extra || now < retryAt)
                    {
                        return;
                    }

                    std::lock_guard<std::mutex> speakerLock (speakerMutex);
                    std::lock_guard<std::mutex> stationLock (stationMutex);

                    if (playEpoch != myEpoch)
                    {
                        return;                 // a new play; the reader is about to restart
                    }

                    if (pending == shownTitle)
                    {
                        pending.clear ();
                        return;
                    }

                    // Only while the speaker is still on what control gave it: never pull it back
                    // from another source or a stop. It may just be between states, so look again
                    // shortly before giving up on this title.
                    if (client.currentStreamUrl () != target)
                    {
                        if (now - pendingSince < DIRECT_GIVE_UP)
                        {
                            retryAt = now + std::chrono::seconds (1);
                            return;
                        }

                        if (!saidAway)
                        {
                            Say () << ">>> speaker is no longer on " << mine->displayName
                                   << "; not updating the display\n";
                            saidAway = true;
                        }

                        pending.clear ();
                        return;
                    }

                    saidAway = false;

                    std::string artist;
                    std::string song;

                    splitStreamTitle (pending, artist, song);

                    Say () << ">>> now playing: " << pending << "\n";

                    lastPushMs = steadyMilliseconds ();

                    if (client.updateTrackInfo (target, mine->displayName, song, artist))
                    {
                        shownTitle = pending;
                        pending.clear ();
                    }
                    else
                    {
                        retryAt = now + PUSH_RETRY;

                        Say (std::cerr) << ">>> could not update the display; retrying in "
                                        << PUSH_RETRY.count () << "s\n";
                    }
                };

                IcyReader reader ([&] (const std::string &title)
                {
                    pending = title;
                    pendingSince = std::chrono::steady_clock::now ();

                    return (true);
                },
                [&] ()
                {
                    if (watcherStop)
                    {
                        return (true);
                    }

                    {
                        std::lock_guard<std::mutex> lock (stationMutex);

                        // Any new play restarts the reader, even of this same station, so the
                        // song comes round again for a display that play just reset.
                        if (playEpoch != myEpoch)
                        {
                            return (true);
                        }
                    }

                    pushSettled ();

                    return (false);
                });

                // Holds the connection until a new play, a stop, or a drop.
                const bool readOk = reader.read (mine->url);

                if (reader.lacksMetadata ())
                {
                    Say () << ">>> " << mine->displayName
                           << " sends no song titles; the display keeps the station name\n";

                    // Nothing more to read until something else is played.
                    while (!watcherStop)
                    {
                        std::this_thread::sleep_for (std::chrono::milliseconds (400));

                        std::lock_guard<std::mutex> lock (stationMutex);

                        if (playEpoch != myEpoch)
                        {
                            break;
                        }
                    }

                    continue;
                }

                if (!readOk)
                {
                    Say (std::cerr) << ">>> song watcher: " << reader.getError () << "\n";
                }

                bool replayed = false;

                {
                    std::lock_guard<std::mutex> lock (stationMutex);

                    replayed = (playEpoch != myEpoch);
                }

                // Follow a new play at once; pause before reconnecting to the same one.
                if (!replayed && !watcherStop)
                {
                    std::this_thread::sleep_for (std::chrono::seconds (1));
                }
            }
        };

        WebSocketListener::Callbacks callbacks;

        callbacks.play = playCallback;
        callbacks.presetInfo = presetInfoCallback;
        callbacks.couldExtend = couldExtendCallback;

        // A press, of a preset on the remote or of the dashboard's play for a stream. Only atomics and
        // the relay, so it is called on the dashboard's threads as well as the listener's.
        auto takePress = [&] ()
        {
            // The press has just stopped the speaker; hold its position there. Whatever it leads
            // to is the user's doing, not something to undo, and it replaces any skip still pending.
            wantPlaying = false;
            pendingPauseMs = 0;
            skipPending = false;
            skipSteps = 0;
            lastPressMs = steadyMilliseconds ();
            ++pressCount;

            // A press ends a pause: what it plays is not the remote's Play, and its new connection
            // must not be taken for the paused speaker reconnecting.
            pausedByUser = false;

            if (proxy.isRunning ())
            {
                proxy.noteSpeakerStopped ();

                // Whatever it plays opens a connection of its own; one that plays nothing, as an
                // unmapped button, leaves the speaker silent, as the press means.
                proxy.cancelRestart ();
            }
        };

        callbacks.press = takePress;

        callbacks.source = [&] (const std::string &source, const std::string &status, const std::string &location)
        {
            {
                std::lock_guard<std::mutex> lock (speakerStateMutex);

                speakerSource = source;
                speakerStatus = status;

                // An event can leave the stream out; another source has none.
                if (source != "UPNP")
                {
                    speakerLocation.clear ();
                }
                else if (!location.empty ())
                {
                    speakerLocation = location;
                }
            }

            // Wakes the dashboard once the rest of this has had its say, whichever way it returns.
            struct NotifyOnReturn
            {
                LiveSignal &signal;

                ~NotifyOnReturn ()
                {
                    signal.notify ();
                }
            } notifyOnReturn { liveSignal };

            // Switched off, or over to something else: that is what the user wants now.
            if (source == "STANDBY" || (source != "UPNP" && source != "INVALID_SOURCE"))
            {
                wantPlaying = false;
                pendingPauseMs = 0;
                onControlStream = false;
                return;
            }

            if (source != "UPNP")
            {
                return;
            }

            if (isStreamLocation (location))
            {
                onControlStream = (streamAt (location) != nullptr);
            }

            // Another app streaming to it has it now; when that ends, it is not control's to undo.
            if (isStreamLocation (location) && !onControlStream)
            {
                wantPlaying = false;
                pendingPauseMs = 0;
                return;
            }

            const long long now = steadyMilliseconds ();
            const long long sinceOwnCommand = now - std::max (lastPushMs.load (), lastPlayMs.load ());
            const bool ownDoing = (sinceOwnCommand
                                   < std::chrono::duration_cast<std::chrono::milliseconds> (OWN_TRANSITION_WINDOW).count ());

            // Paused or stopped from the remote or another app. Just after a command of control's
            // own it may be the speaker on its way to playing that, which it shows by buffering or
            // playing next; until then it is held as a pause that may not be one.
            if ((status == "PAUSE_STATE" || status == "STOP_STATE") && wantPlaying)
            {
                if (ownDoing)
                {
                    long long none = 0;

                    pendingPauseMs.compare_exchange_strong (none, now);
                }
                else
                {
                    pendingPauseMs = 0;
                    confirmPause (now);
                }
            }
            else if (status == "BUFFERING_STATE" || status == "PLAY_STATE")
            {
                pendingPauseMs = 0;

                if (status == "PLAY_STATE")
                {
                    resumeFromPause ();
                }
            }
        };

        // The remote's next or previous button. The speaker has already dropped the stream for want
        // of skipping it, so whatever happens, it is played again: a later song, an earlier one, or
        // where it was. Presses that come while one is being handled add up.
        callbacks.skip = [&] (bool forward)
        {
            if (!onControlStream)
            {
                return;                 // another app's stream is its own business
            }

            pendingPauseMs = 0;
            lastPressMs = steadyMilliseconds ();

            // A press ends a pause (see the preset press).
            pausedByUser = false;

            if (proxy.isRunning ())
            {
                proxy.noteSpeakerStopped ();
            }

            ++skipPresses;
            skipSteps += forward ? 1 : -1;
            skipPending = true;

            // A skip is within whatever is playing, which the play finds for itself.
            g_listener->requestPlay (WebSocketListener::NO_PRESET, WebSocketListener::PlayReason::SKIP);
        };

        callbacks.unexpectedStop = [&] ()
        {
            const bool afterPush = (steadyMilliseconds () - lastPushMs
                                    < std::chrono::duration_cast<std::chrono::milliseconds> (PUSH_BLAME_WINDOW).count ());

            settlePause ();

            if (!wantPlaying || pendingPauseMs != 0 || lastPlayed () == nullptr || (!autoResume && !afterPush))
            {
                return;
            }

            Say () << ">>> The speaker stopped by itself" << (afterPush ? " after a song update" : "")
                   << "; starting it again.\n";

            g_listener->requestPlay (WebSocketListener::NO_PRESET, WebSocketListener::PlayReason::RESUME_DROP);
        };

        callbacks.connected = [&] (bool again)
        {
            // The play brings back what played last itself; these only say why it is asked.
            const int asked = WebSocketListener::NO_PRESET;

            // At start-up, look at what the speaker is doing: bring back what was playing last
            // time, or take over a stream of control's that it is paused on.
            if (!again)
            {
                g_listener->requestPlay (asked, WebSocketListener::PlayReason::RESUME_START);
                return;
            }

            settlePause ();

            // Back from losing the speaker, say through a reboot: if paused, see whether it still is;
            // if playing, carry on.
            if (pausedByUser || pendingPauseMs != 0)
            {
                g_listener->requestPlay (asked, WebSocketListener::PlayReason::RESYNC);
            }
            else if (wantPlaying && autoResume && lastPlayed () != nullptr)
            {
                g_listener->requestPlay (asked, WebSocketListener::PlayReason::RESUME_DROP);
            }
        };

        // The speaker's volume moved, from the knob, the app or the remote. Keep the in-memory level
        // current, and wake the dashboard so it shows the change at once, without asking the speaker.
        callbacks.volume = [&] (int target, int actual, bool muted)
        {
            currentVolumeTarget = target;
            currentVolume = actual;
            currentMuted = muted;

            liveSignal.notify ();
        };

        WebSocketListener listener (deviceIp, callbacks, config.getComboWindowMs ());
        bool connected = false;

        // A stop that came while starting up is acted on here: until now there was nothing to stop.
        {
            std::lock_guard<std::mutex> lock (g_listenerMutex);

            g_listener = &listener;
            connected = listener.connect ();

            if (!connected)
            {
                g_listener = nullptr;
            }
            else if (g_stopRequested)
            {
                listener.stop ();
            }
        }

        if (!connected)
        {
            Say (std::cerr) << "Failed to connect WebSocket listener\n";

            proxy.stop ();

            return (1);
        }

        // Read the volume once at start, so the level is known for the station control takes over
        // before the speaker sends its first volume event. From here on the event stream keeps it
        // current. A brief, quiet look: a speaker that will not answer just leaves it unknown.
        {
            SoundTouchClient volumeClient (deviceIp);
            const SoundTouchClient::Volume vol = volumeClient.volume (2000);

            if (vol.valid)
            {
                currentVolumeTarget = vol.target;
                currentVolume = vol.actual;
                currentMuted = vol.muted;

                Say () << ">>> Volume at start: " << vol.actual << (vol.muted ? " (muted)" : "") << "\n";
            }
        }

        // Likewise what the speaker is doing, for the dashboard's buttons until its first event.
        {
            SoundTouchClient stateClient (deviceIp);
            const SoundTouchClient::NowPlaying now = stateClient.glance (2000);

            if (!now.source.empty ())
            {
                std::lock_guard<std::mutex> lock (speakerStateMutex);

                speakerSource = now.source;
                speakerStatus = now.status;
                speakerLocation = (now.source == "UPNP") ? now.location : std::string ();
            }
        }

        // Started only once connected, so every way out from here passes the join below.
        std::thread watcher;

        if (pushTrackInfo)
        {
            if (proxy.isRunning ())
            {
                {
                    Say line;

                    line << "Song titles will be pushed to the display as each song is heard";

                    if (titleOffsetSeconds != 0)
                    {
                        line << ", " << std::abs (titleOffsetSeconds) << "s " << (titleOffsetSeconds > 0 ? "later" : "earlier")
                             << " than the station's title change";
                    }

                    line << ".\n";
                }

                watcher = std::thread (watchRelay);
            }
            else
            {
                Say () << "Song titles will be pushed to the display (about 10s early "
                       << "without the relay).\n";

                watcher = std::thread (watchDirect);
            }
        }

        // With --web, the same dashboard as the standalone command, but served from inside control
        // so it can show what control is doing live. Started once everything it reports on is up.
        std::unique_ptr<WebServer> webServer;

        if (webEnabled)
        {
            auto liveSnapshot = [&] () -> nlohmann::json
            {
                std::string stationName;
                std::string streamName;
                std::string title;
                std::string source;
                std::string status;
                std::string location;
                std::string last;
                int stationPreset = 0;

                {
                    std::lock_guard<std::mutex> lock (stationMutex);

                    stationName = (station != nullptr) ? station->displayName : std::string ();
                    streamName = (station != nullptr) ? station->name : std::string ();
                    stationPreset = (station != nullptr) ? station->preset : 0;
                    title = shownTitle;
                }

                {
                    std::lock_guard<std::mutex> lock (speakerStateMutex);

                    source = speakerSource;
                    status = speakerStatus;
                    location = speakerLocation;
                }

                {
                    std::lock_guard<std::mutex> lock (lastMutex);

                    last = lastStream;
                }

                const int level = currentVolume.load ();

                // What control is playing, only while the speaker is on one of control's streams: on
                // standby, Bluetooth, AUX or another app's stream, control's last station is not it.
                const bool onOurs = onControlStream.load ();

                nlohmann::json snapshot {
                    { "station", onOurs ? stationName : std::string () },
                    { "station_name", onOurs ? streamName : std::string () },
                    { "title", onOurs ? title : std::string () },
                    { "last_preset", lastPreset.load () },
                    { "last_stream", last },
                    { "want_playing", wantPlaying.load () },
                    { "relay_running", proxy.isRunning () },
                    { "source", source },
                    { "status", status },
                    { "location", location } };

                // Null until the first /volume read or event, so the dashboard shows nothing rather
                // than a made-up level.
                snapshot["volume"] = (level < 0) ? nlohmann::json (nullptr) : nlohmann::json (level);
                snapshot["muted"] = currentMuted.load ();

                // The preset button that stands for what is playing, likewise.
                snapshot["station_preset"] = (onOurs && stationPreset > 0) ? nlohmann::json (stationPreset)
                                                                           : nlohmann::json (nullptr);

                return (snapshot);
            };

            WebServer::Settings settings;

            settings.port = static_cast<int> (webPort);
            settings.bind = webBind;
            settings.version = WEB_VERSION;
            settings.relayPort = relayPort;
            settings.relayBufferMb = bufferMb;
            settings.titleOffsetSeconds = titleOffsetSeconds;
            settings.resume = autoResume;
            settings.embedded = true;
            settings.defaultDeviceIp = deviceIp;

            WebServer::ControlHooks hooks;

            // The play button beside a stream: a press for it, as if it were on a preset of its own.
            hooks.playStream = [&] (const std::string &name)
            {
                const std::shared_ptr<const StreamConfig> streams = currentStreams ();
                const Stream *stream = streams->findByName (name);

                if (stream == nullptr)
                {
                    return (404);
                }

                Say () << "\n>>> " << stream->displayName << " played from the dashboard\n";

                takePress ();
                listener.requestPlay (WebSocketListener::NO_PRESET, WebSocketListener::PlayReason::PRESS, name);

                return (0);
            };

            // A stream list saved from the dashboard, used from now on: the presets, the combos, and
            // each stream the next time it is played. The one playing carries on from the address it
            // was given, but shows a new display name and preset at once.
            hooks.streamsSaved = [&] (const std::string &text)
            {
                auto loaded = std::make_shared<StreamConfig> ();

                if (!loaded->loadFromText (text, true))
                {
                    return;
                }

                const std::shared_ptr<const StreamConfig> streams = loaded;

                {
                    std::lock_guard<std::mutex> lock (streamsMutex);

                    liveStreams = streams;
                }

                listener.setComboWindowMs (streams->getComboWindowMs ());

                {
                    std::lock_guard<std::mutex> lock (stationMutex);

                    const Stream *same = (station != nullptr) ? streams->findByName (station->name) : nullptr;

                    if (same != nullptr && same->url == station->url)
                    {
                        station = holdStream (streams, same);
                    }
                }

                // What to bring back is known by its name, which it keeps; its preset may have moved.
                {
                    std::lock_guard<std::mutex> lock (lastMutex);

                    const Stream *last = lastStream.empty () ? nullptr : streams->findByName (lastStream);

                    if (last != nullptr && last->preset != lastPreset)
                    {
                        lastPreset = last->preset;
                        saveLastPlayed (last->preset, last->name);
                    }
                }

                liveSignal.notify ();
            };

            webServer = std::make_unique<WebServer> (settings, liveSnapshot, &liveSignal, hooks);

            if (webServer->start ())
            {
                Say () << "Web dashboard on http://" << (webBind.empty () ? "0.0.0.0" : webBind)
                       << ":" << webPort << "\n";
            }
            else
            {
                Say (std::cerr) << "Web dashboard failed to start; continuing without it.\n";
                webServer.reset ();
            }
        }

        listener.run ();

        if (webServer)
        {
            webServer->stop ();
        }

        {
            std::lock_guard<std::mutex> lock (g_listenerMutex);

            g_listener = nullptr;
        }

        watcherStop = true;

        if (watcher.joinable ())
        {
            watcher.join ();
        }

        proxy.stop ();

        Say () << "\nStopped.\n";

        return (0);
    }

    Say (std::cerr) << "Unknown command: " << command << "\n\n";
    printUsage (argv[0]);

    return (1);
}

int main (int argc, char *argv[])
{
    // Control mode is long-running, so keep output usable when it is redirected to a file.
    std::cout << std::unitbuf;

    // Once, before any thread: control, the relay and the dashboard all use curl from threads of their own.
    curl_global_init (CURL_GLOBAL_DEFAULT);

    // Where streams.json, devices.json and state.json live: --data-dir <dir> anywhere on the
    // command line, else SOUNDTOUCH_DATA_DIR, else the working directory.
    std::vector<char *> args;
    const char *dataDir = std::getenv ("SOUNDTOUCH_DATA_DIR");

    for (int i = 0; i < argc; ++i)
    {
        if (std::string (argv[i]) == "--data-dir")
        {
            if (i + 1 >= argc)
            {
                Say (std::cerr) << "Error: --data-dir needs a directory\n";
                return (1);
            }

            dataDir = argv[++i];
            continue;
        }

        if (std::string (argv[i]).compare (0, 11, "--data-dir=") == 0)
        {
            dataDir = argv[i] + 11;
            continue;
        }

        args.push_back (argv[i]);
    }

    if (dataDir != nullptr && *dataDir != '\0' && chdir (dataDir) != 0)
    {
        Say (std::cerr) << "Error: cannot use data directory " << dataDir << ": " << std::strerror (errno) << "\n";
        return (1);
    }

    args.push_back (nullptr);

    return (handleCommand (static_cast<int> (args.size ()) - 1, args.data ()));
}
