#include "WebSocketListener.h"
#include "Say.h"
#include <libwebsockets.h>
#include <pugixml.hpp>
#include <iostream>
#include <cstring>

WebSocketListener::WebSocketListener (const std::string &deviceIp, const Callbacks &callbacks, int comboWindowMs)
    : m_deviceIp (deviceIp),
      m_wsUrl ("ws://" + deviceIp + ":8080"),
      m_callbacks (callbacks),
      m_comboWindowMs (comboWindowMs),
      m_context (nullptr),
      m_wsi (nullptr),
      m_state (State::IDLE),
      m_pendingPreset (0),
      m_sequence (0),
      m_invalidSeenSincePress (false),
      m_running (false),
      m_stopRequested (false),
      m_lastPressTime (),
      m_playBusy (false),
      m_reconnectTimer (),
      m_reconnecting (false),
      m_everConnected (false)
{
    m_reconnectTimer.self = this;
}

WebSocketListener::~WebSocketListener ()
{
    stop ();

    if (m_playThread.joinable ())
    {
        m_playThread.join ();
    }

    if (m_context != nullptr)
    {
        lws_sul_cancel (&m_reconnectTimer.sul);
        lws_context_destroy (m_context);
        m_context = nullptr;
    }
}

bool WebSocketListener::parsePresetEvent (const std::string &message, int &presetId)
{
    pugi::xml_document doc;

    if (!doc.load_string (message.c_str ()))
    {
        return (false);
    }

    const pugi::xml_node preset = doc.select_node ("//nowSelectionUpdated/preset").node ();

    if (!preset)
    {
        return (false);
    }

    const pugi::xml_attribute id = preset.attribute ("id");

    if (!id)
    {
        return (false);
    }

    presetId = id.as_int ();

    return (presetId != 0);
}

bool WebSocketListener::parseNowPlayingSource (const std::string &message, std::string &source,
                                               std::string &status, std::string &location)
{
    pugi::xml_document doc;

    if (!doc.load_string (message.c_str ()))
    {
        return (false);
    }

    const pugi::xml_node nowPlaying = doc.select_node ("//nowPlayingUpdated/nowPlaying").node ();

    if (!nowPlaying)
    {
        return (false);
    }

    source = nowPlaying.attribute ("source").value ();
    status = nowPlaying.child_value ("playStatus");
    location = nowPlaying.child ("ContentItem").attribute ("location").value ();

    return (true);
}

// The speaker's report that it could not skip, which is how a press of the remote's next or
// previous button shows on a stream it was handed:
//   <errorUpdate ...><error value="4301" name="QPLAY_SKIP_NEXT_FAILED" ...>SkipNext failed.</error>
//   <errorUpdate ...><error value="4302" name="QPLAY_SKIP_PREV_FAILED" ...>SkipPrev failed.</error>
bool WebSocketListener::parseSkipFailure (const std::string &message, bool &forward)
{
    pugi::xml_document doc;

    if (!doc.load_string (message.c_str ()))
    {
        return (false);
    }

    const pugi::xml_node error = doc.select_node ("//errorUpdate/error").node ();

    if (!error)
    {
        return (false);
    }

    const std::string name = error.attribute ("name").value ();
    const int value = error.attribute ("value").as_int ();

    if (name == "QPLAY_SKIP_NEXT_FAILED" || value == 4301)
    {
        forward = true;
        return (true);
    }

    if (name == "QPLAY_SKIP_PREV_FAILED" || value == 4302)
    {
        forward = false;
        return (true);
    }

    return (false);
}

// The speaker's volume report, pushed whenever the level or mute changes:
//   <updates ...><volumeUpdated><volume><targetvolume>15</targetvolume>
//   <actualvolume>15</actualvolume><muteenabled>false</muteenabled></volume></volumeUpdated></updates>
bool WebSocketListener::parseVolume (const std::string &message, int &target, int &actual, bool &muted)
{
    pugi::xml_document doc;

    if (!doc.load_string (message.c_str ()))
    {
        return (false);
    }

    const pugi::xml_node volume = doc.select_node ("//volumeUpdated/volume").node ();

    if (!volume)
    {
        return (false);
    }

    target = volume.child ("targetvolume").text ().as_int ();
    actual = volume.child ("actualvolume").text ().as_int ();
    muted = (std::strcmp (volume.child_value ("muteenabled"), "true") == 0);

    return (true);
}

void WebSocketListener::armTimer (int milliseconds)
{
    if (m_wsi != nullptr)
    {
        lws_set_timer_usecs (m_wsi, static_cast<lws_usec_t> (milliseconds) * 1000);
    }
}

void WebSocketListener::cancelTimer ()
{
    if (m_wsi != nullptr)
    {
        lws_set_timer_usecs (m_wsi, LWS_SET_TIMER_USEC_CANCEL);
    }
}

void WebSocketListener::startPlayback (int presetId)
{
    cancelTimer ();

    m_state = State::IDLE;
    m_pendingPreset = 0;

    requestPlay (presetId, PlayReason::PRESS);
}

void WebSocketListener::requestPlay (int presetId, PlayReason reason, const std::string &streamName)
{
    {
        std::lock_guard<std::mutex> lock (m_playMutex);

        m_playRequest.pending = true;
        m_playRequest.presetId = presetId;
        m_playRequest.reason = reason;
        m_playRequest.streamName = streamName;
    }

    // lws_service() keeps sleeping after firing a callback, so the loop would not notice this
    // request until some unrelated event arrived. Break the wait explicitly; this is the one lws
    // call made for calling from any thread.
    lws_cancel_service (m_context);
}

void WebSocketListener::onTimer ()
{
    if (m_sequence != 0)
    {
        Say () << ">>> No further digit arrived.\n";

        resolveSequence ();

        return;
    }

    if (m_state == State::WAITING_FOR_INVALID)
    {
        Say () << "\n>>> Gave up waiting for the speaker to finish preset "
               << m_pendingPreset << ".\n";
        Say () << ">>> Not starting playback; the expected transition never happened.\n\n";

        m_state = State::IDLE;
        m_pendingPreset = 0;
    }
}

// Decide what the digits pressed so far mean, and either play now or wait for the speaker's
// failing preset attempt to land first.
void WebSocketListener::resolveSequence ()
{
    const int preset = m_sequence;

    m_sequence = 0;

    cancelTimer ();

    std::string presetName;

    if (!m_callbacks.presetInfo || !m_callbacks.presetInfo (preset, presetName))
    {
        Say () << ">>> No stream configured for preset " << preset << "; ignoring\n\n";

        m_state = State::IDLE;
        m_pendingPreset = 0;

        return;
    }

    Say () << ">>> Preset " << preset << " -> " << presetName << "\n";

    // Playing before the speaker's own attempt fails lets that failure wipe our metadata, which
    // loses the station name on the display. If the failure already arrived, go straight ahead.
    if (m_invalidSeenSincePress)
    {
        Say () << ">>> Speaker already gave up on it; starting playback.\n\n";

        startPlayback (preset);
    }
    else
    {
        Say () << ">>> Waiting for the speaker to finish its own preset handling...\n\n";

        m_state = State::WAITING_FOR_INVALID;
        m_pendingPreset = preset;

        armTimer (PRESET_TIMEOUT * 1000);
    }
}

void WebSocketListener::onButtonPressed (int button)
{
    m_lastPressTime = std::chrono::steady_clock::now ();

    if (m_callbacks.press)
    {
        m_callbacks.press ();
    }

    // Each press starts a fresh wait for that press's INVALID_SOURCE.
    m_invalidSeenSincePress = false;

    m_sequence = (m_sequence * 10) + button;

    Say () << "\n>>> Button " << button << " pressed; sequence so far: " << m_sequence << "\n";

    // Keep collecting only while some configured preset is longer than what we have and starts
    // with it. Once nothing can extend it, the sequence is final.
    if (m_callbacks.couldExtend && m_callbacks.couldExtend (m_sequence))
    {
        const int window = m_comboWindowMs;

        armTimer (window);

        Say () << ">>> Waiting " << window << "ms for another digit...\n";

        return;
    }

    resolveSequence ();
}

void WebSocketListener::handleEvent (const std::string &message)
{
    Say () << "EVENT:\n" << message << "\n" << std::string (70, '-') << "\n";

    std::string source;
    std::string status;
    std::string location;

    if (parseNowPlayingSource (message, source, status, location) && m_callbacks.source)
    {
        m_callbacks.source (source, status, location);
    }

    // A skip button: the stream it drops next is the press's doing, not the speaker giving up.
    bool forward = false;

    if (parseSkipFailure (message, forward))
    {
        m_lastPressTime = std::chrono::steady_clock::now ();

        Say () << ">>> " << (forward ? "Next" : "Previous") << " pressed on the remote\n";

        if (m_callbacks.skip)
        {
            m_callbacks.skip (forward);
        }

        return;
    }

    // Volume: the knob, the app or the remote moved it. Only in-memory state to update.
    int volTarget = 0;
    int volActual = 0;
    bool volMuted = false;

    if (parseVolume (message, volTarget, volActual, volMuted))
    {
        if (m_callbacks.volume)
        {
            m_callbacks.volume (volTarget, volActual, volMuted);
        }

        return;
    }

    if (source == "INVALID_SOURCE")
    {
        m_invalidSeenSincePress = true;

        if (m_state != State::WAITING_FOR_INVALID)
        {
            // Not the tail of a press, nor something control is starting: the speaker gave up on
            // what it was playing all by itself.
            const bool afterPress = (std::chrono::steady_clock::now () - m_lastPressTime < PRESS_GRACE);

            if (!afterPress && m_sequence == 0 && !m_playBusy && m_callbacks.unexpectedStop)
            {
                Say () << ">>> INVALID_SOURCE with no button pressed\n";
                m_callbacks.unexpectedStop ();
                return;
            }

            Say () << ">>> INVALID_SOURCE noted (nothing pending yet)\n";
            return;
        }

        Say () << "\n>>> Preset " << m_pendingPreset << " failed on the speaker (cloud is gone).\n";
        Say () << ">>> Taking over with direct UPnP playback.\n\n";

        startPlayback (m_pendingPreset);

        return;
    }

    int button = 0;

    if (parsePresetEvent (message, button))
    {
        onButtonPressed (button);
    }
}

int WebSocketListener::webSocketCallback (
    struct lws *wsi,
    enum lws_callback_reasons reason,
    void *user,
    void *in,
    size_t len
)
{
    WebSocketListener *listener = static_cast<WebSocketListener *> (lws_context_user (lws_get_context (wsi)));

    (void) user;

    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED :
        {
            if (listener != nullptr)
            {
                listener->onConnected ();
            }

            break;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE :
        {
            if (listener != nullptr)
            {
                listener->handleEvent (std::string (static_cast<const char *> (in), len));
            }

            break;
        }

        case LWS_CALLBACK_TIMER :
        {
            if (listener != nullptr)
            {
                listener->onTimer ();
            }

            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR :
        {
            // Said once per outage: it is tried again every few seconds until the speaker is back.
            if (listener == nullptr || !listener->m_reconnecting)
            {
                Say (std::cerr) << "\nWebSocket connection error"
                                << ((in != nullptr) ? std::string (": ") + static_cast<const char *> (in) : std::string ())
                                << "\n";
            }

            if (listener != nullptr)
            {
                listener->onDisconnected ();
            }

            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED :
        {
            Say () << "\nWebSocket connection closed\n";

            if (listener != nullptr)
            {
                listener->onDisconnected ();
            }

            break;
        }

        default :
        {
            break;
        }
    }

    return (0);
}

bool WebSocketListener::connect ()
{
    static const struct lws_protocols protocols[] =
    {
        {
            "gabbo",
            webSocketCallback,
            0,
            4096,
            0,
            nullptr,
            0
        },
        { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
    };

    struct lws_context_creation_info info;

    // Errors and warnings only: its notices repeat every failed attempt while the speaker is away.
    lws_set_log_level (LLL_ERR | LLL_WARN, nullptr);

    std::memset (&info, 0, sizeof (info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = 0;
    info.user = this;

    m_context = lws_create_context (&info);

    if (m_context == nullptr)
    {
        Say (std::cerr) << "Failed to create libwebsockets context\n";
        return (false);
    }

    // The speaker may not be reachable yet, say mid-boot or before the network is up. Keep trying,
    // as after losing it later; only a context that cannot be created is fatal.
    if (!openConnection ())
    {
        onDisconnected ();
    }

    return (true);
}

bool WebSocketListener::openConnection ()
{
    struct lws_client_connect_info ccinfo;

    std::memset (&ccinfo, 0, sizeof (ccinfo));

    ccinfo.context = m_context;
    ccinfo.address = m_deviceIp.c_str ();
    ccinfo.port = 8080;
    ccinfo.path = "/";
    ccinfo.host = m_deviceIp.c_str ();
    ccinfo.origin = m_deviceIp.c_str ();
    ccinfo.protocol = "gabbo";
    ccinfo.pwsi = &m_wsi;

    m_wsi = lws_client_connect_via_info (&ccinfo);

    return (m_wsi != nullptr);
}

void WebSocketListener::onConnected ()
{
    const bool again = m_everConnected;

    if (m_everConnected)
    {
        Say () << "\n>>> Reconnected to the speaker; listening for buttons again.\n";
        Say () << std::string (70, '-') << "\n";
    }
    else
    {
        Say () << "\nCONNECTED\n\n";
        Say () << "Listening for physical preset buttons. Press Ctrl-C to stop.\n";
        Say () << std::string (70, '-') << "\n";
    }

    m_reconnecting = false;
    m_everConnected = true;

    if (m_callbacks.connected)
    {
        m_callbacks.connected (again);
    }
}

void WebSocketListener::onDisconnected ()
{
    m_wsi = nullptr;

    // A combo being typed, or a wait for the speaker's own preset handling, cannot finish
    // without its events.
    m_sequence = 0;
    m_state = State::IDLE;
    m_pendingPreset = 0;

    if (m_stopRequested)
    {
        return;
    }

    if (!m_reconnecting)
    {
        if (m_everConnected)
        {
            Say () << ">>> Lost the speaker's events (rebooting, or off the network?); retrying every "
                   << RECONNECT_SECONDS << "s\n";
        }
        else
        {
            Say () << ">>> Cannot reach the speaker at " << m_deviceIp << " yet; retrying every "
                   << RECONNECT_SECONDS << "s\n";
        }

        m_reconnecting = true;
    }

    scheduleReconnect ();
}

void WebSocketListener::scheduleReconnect ()
{
    lws_sul_schedule (m_context, 0, &m_reconnectTimer.sul, reconnectCallback,
                      static_cast<lws_usec_t> (RECONNECT_SECONDS) * LWS_USEC_PER_SEC);
}

void WebSocketListener::reconnectCallback (lws_sorted_usec_list_t *sul)
{
    WebSocketListener *self = reinterpret_cast<ReconnectTimer *> (sul)->self;

    if (self->m_stopRequested)
    {
        return;
    }

    // A refusal can come back at once, or later as a connection error; either way, try again.
    if (!self->openConnection ())
    {
        self->scheduleReconnect ();
    }
}

void WebSocketListener::run ()
{
    m_running = true;

    Say () << "\nConnecting to " << m_wsUrl << "\n";

    while (m_running && !m_stopRequested)
    {
        lws_service (m_context, 0);

        // While a playback is in flight, leave the request waiting so it is picked up when that
        // finishes. A newer press simply overwrites it, so the last button pressed wins.
        if (!m_playBusy)
        {
            PlayRequest request;

            {
                std::lock_guard<std::mutex> lock (m_playMutex);

                request = m_playRequest;
                m_playRequest.pending = false;
            }

            if (request.pending && m_callbacks.play)
            {
                if (m_playThread.joinable ())
                {
                    m_playThread.join ();
                }

                m_playBusy = true;

                // Runs off the service loop so the HTTP calls do not stall the WebSocket.
                m_playThread = std::thread ([this, request] ()
                {
                    // A resume that finds nothing to do reports why itself.
                    if (!m_callbacks.play (request.presetId, request.reason, request.streamName)
                        && request.reason == PlayReason::PRESS)
                    {
                        if (request.streamName.empty ())
                        {
                            Say (std::cerr) << ">>> Failed to start preset " << request.presetId << "\n";
                        }
                        else
                        {
                            Say (std::cerr) << ">>> Failed to start " << request.streamName << "\n";
                        }
                    }

                    m_playBusy = false;

                    // Wake the loop so a request queued while this was in flight runs at once.
                    lws_cancel_service (m_context);
                });
            }
        }

    }

    if (m_playThread.joinable ())
    {
        if (m_playBusy)
        {
            Say () << "Waiting for the playback request in flight to finish...\n";
        }

        m_playThread.join ();
    }

    m_running = false;
}

void WebSocketListener::stop ()
{
    m_stopRequested = true;

    // lws_service() would otherwise sleep on until the speaker next sent something.
    if (m_context != nullptr)
    {
        lws_cancel_service (m_context);
    }
}
