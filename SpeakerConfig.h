#ifndef SPEAKER_CONFIG_H
#define SPEAKER_CONFIG_H

#include "DeviceDiscovery.h"
#include <string>
#include <vector>

// Definition of a saved speaker zone (multiroom group).
struct ZoneConfig
{
    std::string              id;
    std::string              name;
    std::string              leaderId;
    std::vector<std::string> memberIds; // speaker IDs including leader

    bool operator== (const ZoneConfig &other) const
    {
        return (id == other.id && name == other.name && leaderId == other.leaderId && memberIds == other.memberIds);
    }
};

// Playback target specification: either a solo speaker or a defined zone.
struct PlaybackTarget
{
    enum class Type
    {
        Speaker,
        Zone
    };

    Type        type = Type::Speaker;
    std::string id;

    bool operator== (const PlaybackTarget &other) const
    {
        return (type == other.type && id == other.id);
    }

    bool operator!= (const PlaybackTarget &other) const
    {
        return (!(*this == other));
    }
};

// Pure resolution of a playback target into concrete device details.
struct ResolvedTarget
{
    std::string              leaderIp;
    std::string              leaderDeviceId;
    std::string              leaderDeviceName;
    bool                     isZone = false;
    bool                     autoResume = true; // true for solo speaker; false for zone until verified
    std::string              zoneId;
    std::string              zoneName;
    std::vector<std::string> memberDeviceIds;   // all members including leader
    std::vector<std::string> memberIps;
};

// The speakers this program knows, as devices.json keeps them: each speaker found on the network
// and saved, defined multiroom zones, and which target (speaker or zone) is the default.
// Free of sockets and of discovery itself, so it can be unit-tested on hand-built speakers.
class SpeakerConfig
{
    public :

        SpeakerConfig ();

        // Takes the speakers, zones and targets from the text of devices.json.
        bool loadFromText (const std::string &text);

        const std::vector<SoundTouchDevice> &getSpeakers () const { return (m_speakers); }

        // The default as the file names it, "" for none.
        const std::string &getDefaultId () const { return (m_defaultId); }

        // The default speaker: the one the file names, else the first, as every command takes it.
        // nullptr when there is none.
        const SoundTouchDevice *getDefaultSpeaker () const;

        const SoundTouchDevice *findById (const std::string &deviceId) const;

        // Adds a speaker found on the network, or brings a saved one up to date.
        bool remember (const SoundTouchDevice &speaker);

        // Makes a saved speaker the default.
        bool setDefault (const std::string &deviceId);

        // Zone configuration management
        const std::vector<ZoneConfig> &getZones () const { return (m_zones); }
        const ZoneConfig             *findZoneById (const std::string &zoneId) const;
        const ZoneConfig             *findZoneByName (const std::string &name) const;

        bool validateZone (const ZoneConfig &zone, std::string &error, const std::string &excludeZoneId = "") const;
        bool addZone (const ZoneConfig &zone, std::string &error);
        bool updateZone (const ZoneConfig &zone, std::string &error);
        bool removeZone (const std::string &zoneId, std::string &error);

        // Playback target management
        const PlaybackTarget &getDefaultTarget () const { return (m_defaultTarget); }
        bool                  hasDefaultTarget () const { return (m_hasDefaultTarget); }
        bool                  setDefaultTarget (const PlaybackTarget &target, std::string &error);

        // Resolves default target or arbitrary target into leader device and members.
        bool resolveDefaultTarget (ResolvedTarget &out) const;
        bool resolveTarget (const PlaybackTarget &target, ResolvedTarget &out, std::string &error) const;

        // Target JSON helpers
        static bool        parseTargetJson (const std::string &jsonStr, PlaybackTarget &target, std::string &error);
        static std::string targetToJson (const PlaybackTarget &target);

        // The text of devices.json for these speakers and zones. Preserves unknown fields and keys.
        std::string fileText (const std::string &existing) const;

    private :

        // Now the data members

        std::vector<SoundTouchDevice> m_speakers;
        std::string                   m_defaultId;
        std::vector<ZoneConfig>       m_zones;
        PlaybackTarget                m_defaultTarget;
        bool                          m_hasDefaultTarget = false;
};

#endif
