#ifndef SOUNDTOUCH_CLIENT_H
#define SOUNDTOUCH_CLIENT_H

#include <string>
#include <vector>
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
        // whatever its status, plus any track/artist/album metadata supplied by the speaker.
        // All empty if the speaker could not be asked.
        struct NowPlaying
        {
            std::string source;
            std::string status;
            std::string location;
            std::string track;
            std::string artist;
            std::string album;
        };

        // Member entry of a multiroom zone.
        struct ZoneMember
        {
            std::string deviceId;
            std::string ipAddress;
            int         role = 0;
            bool        isLeader = false;

            bool operator== (const ZoneMember &other) const
            {
                return (deviceId == other.deviceId && ipAddress == other.ipAddress);
            }
        };

        // Normalized zone status as reported by Bose /getZone. Leader is always normalized
        // into members even if the device firmware omitted it.
        struct ZoneStatus
        {
            bool                    inZone = false;
            bool                    isLeader = false;
            std::string             masterId;
            std::string             senderIpAddress;
            std::vector<ZoneMember> members;
        };

        struct PresetItem
        {
            int         id = 0;
            std::string location;
            std::string name;
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
        bool        getPresets (std::vector<PresetItem> &items, long timeoutMs = 10000);
        bool        savePreset (int presetId, const std::string &streamUrl, const std::string &stationName);
        // Presses the buttons that spell out this preset, spacing them so a multi-digit preset is
        // read as one combo. Only has an effect on playback while "control" is running.
        bool        selectPreset (int preset, int gapMs);
        std::string currentStreamUrl ();
        NowPlaying  nowPlaying ();

        // The same, for a look taken often: gives up after timeoutMs, and does not report failing.
        NowPlaying  glance (long timeoutMs);

        // What /volume says: the level the speaker is moving to, the level it is at now (both 0..100),
        // and whether it is muted. valid is false when the speaker could not be asked.
        struct Volume
        {
            int  target;
            int  actual;
            bool muted;
            bool valid;

            Volume ()
                : target (0),
                  actual (0),
                  muted (false),
                  valid (false)
            {
            }
        };

        // Reads /volume; gives up after timeoutMs, and below the default stays quiet about failing,
        // like glance, so the dashboard's frequent polls do not fill the log.
        Volume      volume (long timeoutMs = 10000);

        // Sets the volume, 0 to 100, giving up after timeoutMs. The speaker then reports the new level
        // on its event stream, as it does for the knob or the remote.
        bool        setVolume (int level, long timeoutMs = 10000);

        // Presses and releases a key of the remote, by its Web API name (PLAY, PAUSE, POWER, ...), as
        // the press and release POSTs to /key the API asks for; each may take timeoutMs.
        bool        pressKey (const std::string &key, long timeoutMs = 10000);

        // Switches to one of the speaker's own sources, as its source buttons do, through /select:
        // BLUETOOTH (no account), or AUX (account AUX).
        bool        selectSource (const std::string &source, const std::string &account, long timeoutMs = 10000);

        // Discovers local route IP used to reach this speaker without invoking shell.
        std::string getLocalRouteIp () const;

        // Native multiroom zone management APIs
        bool        getZone (ZoneStatus &status, long timeoutMs = 10000, std::string *errorOut = nullptr);
        bool        setZone (const std::string &masterId, const std::string &senderIpAddress,
                             const std::vector<ZoneMember> &followers, long timeoutMs = 10000,
                             std::string *errorOut = nullptr);
        bool        addZoneSlave (const std::string &masterId, const ZoneMember &follower,
                                  long timeoutMs = 10000, std::string *errorOut = nullptr);
        bool        removeZoneSlave (const std::string &masterId, const ZoneMember &follower,
                                     long timeoutMs = 10000, std::string *errorOut = nullptr);

        // Static XML builders and parsers for testing and native communication
        static std::string buildSetZoneXml (const std::string &masterId, const std::string &senderIpAddress,
                                            const std::vector<ZoneMember> &followers);
        static std::string buildZoneSlaveXml (const std::string &masterId, const ZoneMember &follower);
        static bool        parseZoneXml (const std::string &xml, const std::string &clientIp, ZoneStatus &status,
                                         std::string *errorOut = nullptr);
        static bool        parseBoseErrors (const std::string &xml, std::string &errorMessage);
        static bool        parseNowPlayingXml (const std::string &xml, NowPlaying &now);

        // Low-level HTTP requests with HTTP status code reporting
        bool restGet (const std::string &endpoint, std::string &response, long timeoutMs = 10000,
                      bool quiet = false, long *httpStatusOut = nullptr);
        bool restPost (const std::string &endpoint, const std::string &body, std::string &response,
                       long timeoutMs = 10000, long *httpStatusOut = nullptr);

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

        // Now the data members

        std::string m_deviceIp;
        std::string m_restUrl;
        std::string m_controlUrl;

        // How long the speaker may take to start a stream: ~1.5s through the relay, ~4s from the
        // CDN directly.
        static constexpr int START_TIMEOUT_MS = 8000;
};

#endif
