#ifndef WEBSOCKET_LISTENER_H
#define WEBSOCKET_LISTENER_H

#include <string>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <libwebsockets.h>

class WebSocketListener
{
    public :

        // Why a play was asked for.
        enum class PlayReason
        {
            PRESS,              // a button, or a combo of them
            RESUME_START,       // control has just started
            RESUME_DROP,        // the speaker stopped by itself, or is back from a reboot
            RESYNC,             // only look at what the speaker is doing; never plays
            SKIP                // the remote's next or previous button
        };

        // Asked with this, a look at the speaker has no preset to bring back.
        static constexpr int NO_PRESET = -1;

        // Plays the stream mapped to a preset, or, given a stream's name, that stream.
        using PlayCallback = std::function<bool(int, PlayReason, const std::string &)>;

        // Fills in the stream name for a preset; false when the preset has no stream configured.
        using PresetInfoCallback = std::function<bool(int, std::string &)>;

        // True when a longer preset starts with this digit sequence, so another digit may follow.
        using CouldExtendCallback = std::function<bool(int)>;

        // Told of every button press as it arrives. The speaker has just stopped playback.
        using PressCallback = std::function<void()>;

        // Told of the source each now-playing event reports (UPNP, STANDBY, BLUETOOTH and so on),
        // its play status (PLAY_STATE, PAUSE_STATE, ...; empty when the event has none) and, for a
        // stream, the URL the speaker was given (empty when the event has none).
        using SourceCallback = std::function<void(const std::string &, const std::string &, const std::string &)>;

        // The speaker dropped to INVALID_SOURCE with no button pressed and no play in flight.
        using UnexpectedStopCallback = std::function<void()>;

        // The event stream is up; true when it is back after being lost.
        using ConnectedCallback = std::function<void(bool)>;

        // The remote's next (true) or previous (false) button was pressed. The speaker cannot skip
        // a stream it was handed: it tries, reports that it failed, and drops it.
        using SkipCallback = std::function<void(bool)>;

        // The speaker's volume changed, from the knob, the app or the remote: the level it is moving
        // to and the level it is at now (both 0..100), and whether it is muted.
        using VolumeCallback = std::function<void(int target, int actual, bool muted)>;

        // Only play is required.
        struct Callbacks
        {
            PlayCallback           play;
            PresetInfoCallback     presetInfo;
            CouldExtendCallback    couldExtend;
            PressCallback          press;
            SourceCallback         source;
            UnexpectedStopCallback unexpectedStop;
            ConnectedCallback      connected;
            SkipCallback           skip;
            VolumeCallback         volume;
        };

        WebSocketListener (const std::string &deviceIp, const Callbacks &callbacks, int comboWindowMs);
        ~WebSocketListener ();

        // Asks for a play other than from a button, as a press would, or with RESYNC only a look; given
        // streamName, a press for that stream rather than for a preset. The latest request wins over
        // any not yet taken up. Safe from any thread.
        void requestPlay (int presetId, PlayReason reason, const std::string &streamName = std::string ());

        // How long to wait for another digit of a combo, from the next press on. Safe from any thread.
        void setComboWindowMs (int comboWindowMs) { m_comboWindowMs = comboWindowMs; }

        bool connect ();
        void run ();
        void stop ();

    private :

        enum class State
        {
            IDLE,
            WAITING_FOR_INVALID
        };

        void handleEvent (const std::string &message);
        bool parsePresetEvent (const std::string &message, int &presetId);
        bool parseNowPlayingSource (const std::string &message, std::string &source, std::string &status,
                                    std::string &location);
        bool parseSkipFailure (const std::string &message, bool &forward);
        bool parseVolume (const std::string &message, int &target, int &actual, bool &muted);
        void onButtonPressed (int button);
        void onTimer ();

        // The speaker's event stream drops when it reboots or leaves the network; keep trying to
        // get it back rather than sit deaf.
        bool openConnection ();
        void onConnected ();
        void onDisconnected ();
        void scheduleReconnect ();
        static void reconnectCallback (lws_sorted_usec_list_t *sul);
        void resolveSequence ();
        void startPlayback (int presetId);

        // lws_service() sleeps until an event arrives, so waiting has to be scheduled through lws
        // rather than polled from the loop. Only one deadline is ever pending: either we are
        // collecting digits or we are waiting for INVALID_SOURCE, never both.
        void armTimer (int milliseconds);
        void cancelTimer ();

        static int webSocketCallback (
            struct lws *wsi,
            enum lws_callback_reasons reason,
            void *user,
            void *in,
            size_t len
        );

        // lws hands a scheduled callback only its list entry, so the entry comes first and the
        // listener rides along after it.
        struct ReconnectTimer
        {
            lws_sorted_usec_list_t  sul;
            WebSocketListener      *self;
        };

        // A play asked for and not yet taken up by the loop.
        struct PlayRequest
        {
            bool        pending = false;
            int         presetId = 0;
            PlayReason  reason = PlayReason::PRESS;
            std::string streamName;
        };

        // Now the data members

        std::string             m_deviceIp;
        std::string             m_wsUrl;
        Callbacks               m_callbacks;
        std::atomic<int>        m_comboWindowMs;
        struct lws_context     *m_context;
        struct lws             *m_wsi;
        State                   m_state;
        int                     m_pendingPreset;
        int                     m_sequence;
        bool                    m_invalidSeenSincePress;
        std::atomic<bool>       m_running;
        std::atomic<bool>       m_stopRequested;
        std::mutex              m_playMutex;        // m_playRequest
        PlayRequest             m_playRequest;
        std::chrono::steady_clock::time_point m_lastPressTime;
        std::atomic<bool>       m_playBusy;
        std::thread             m_playThread;
        ReconnectTimer          m_reconnectTimer;
        bool                    m_reconnecting;
        bool                    m_everConnected;

        static constexpr int    PRESET_TIMEOUT = 10;

        // An INVALID_SOURCE this soon after a press is the press's own failed attempt.
        static constexpr std::chrono::seconds PRESS_GRACE { 3 };
        static constexpr int    RECONNECT_SECONDS = 3;
};

#endif
