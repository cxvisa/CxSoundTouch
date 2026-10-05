#ifndef SOUNDTOUCH_CLIENT_H
#define SOUNDTOUCH_CLIENT_H

#include <string>
#include <functional>

// Returns the stream URL from a nowPlaying XML document if a UPnP stream is playing, else "".
std::string playingStreamUrl (const std::string &nowPlayingXml);

class SoundTouchClient
{
    public :

        explicit SoundTouchClient (const std::string &deviceIp);
        ~SoundTouchClient ();

        // True if something newer, such as another button press, has taken over from this play.
        using Superseded = std::function<bool()>;

        // What /nowPlaying says: the source (UPNP, STANDBY, BLUETOOTH, INVALID_SOURCE and so on),
        // its play status (PLAY_STATE, PAUSE_STATE, ...), and for a stream the URL it was given,
        // whatever its status. All empty if the speaker could not be asked.
        struct NowPlaying
        {
            std::string source;
            std::string status;
            std::string location;
        };

        // Called before a stream that did not start is stopped and sent once more, so whatever the
        // first send was set up with (such as where the relay starts it) can be set up again.
        using BeforeRetry = std::function<void()>;

        // When title/artist are given the display cycles song -> artist -> station; otherwise all
        // three fields carry the station name. Returns false, without retrying, as soon as
        // superseded says so.
        bool        playStream (const std::string &streamUrl, const std::string &stationName,
                                const std::string &title = std::string (),
                                const std::string &artist = std::string (),
                                const Superseded &superseded = Superseded (),
                                const BeforeRetry &beforeRetry = BeforeRetry ());

        // Re-sends the stream with song metadata so the display shows it. This re-fetches the
        // stream, so it costs a few seconds of silence each time it is called.
        bool        updateTrackInfo (const std::string &streamUrl, const std::string &stationName,
                                     const std::string &title, const std::string &artist);
        bool        stop ();
        bool        status ();
        bool        presets ();
        bool        savePreset (int presetId, const std::string &streamUrl, const std::string &stationName);
        // Presses the buttons that spell out this preset, spacing them so a multi-digit preset is
        // read as one combo. Only has an effect on playback while "control" is running.
        bool        selectPreset (int preset, int gapMs);
        std::string currentStreamUrl ();
        NowPlaying  nowPlaying ();

        // The same, for a look taken often: gives up after timeoutMs, and does not report failing.
        NowPlaying  glance (long timeoutMs);

    private :

        bool        pressButton (int button);

        std::string buildSetAVTransportURISoap (const std::string &streamUrl, const std::string &title,
                                                const std::string &artist, const std::string &album) const;
        bool        sendStream (const std::string &streamUrl, const std::string &title,
                                const std::string &artist, const std::string &album);

        // The speaker can accept both commands and still not play: its player can get stuck,
        // answering everything and doing nothing. True once it is really playing this URL.
        bool        waitUntilPlaying (const std::string &streamUrl, const Superseded &superseded);
        bool        stopQuietly ();
        std::string buildPlaySoap () const;
        std::string buildStopSoap () const;
        std::string buildStorePresetXml (int presetId, const std::string &streamUrl, const std::string &stationName) const;
        std::string buildKeyPressXml (int presetId, const std::string &state) const;

        bool upnpRequest (const std::string &soapAction, const std::string &body, std::string &response);
        bool restGet (const std::string &endpoint, std::string &response, long timeoutMs = 10000, bool quiet = false);
        bool restPost (const std::string &endpoint, const std::string &body, std::string &response);

        // Now the data members

        std::string m_deviceIp;
        std::string m_restUrl;
        std::string m_controlUrl;

        // How long the speaker may take to start a stream: ~1.5s through the relay, ~4s from the
        // CDN directly.
        static constexpr int START_TIMEOUT_MS = 8000;
};

#endif
