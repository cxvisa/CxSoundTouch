#include "SpeakerConfig.h"
#include <map>
#include <set>
#include <utility>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace
{
    using ordered = nlohmann::ordered_json;

    // A string field of a JSON object, or "" when it is missing or not a string.
    std::string stringField (const ordered &object, const char *key)
    {
        const auto found = object.find (key);

        return ((found != object.end () && found->is_string ()) ? found->get<std::string> () : std::string ());
    }

    // The fields of a speaker this program writes; any others on it are kept as they were.
    const char *const KNOWN_FIELDS[] = { "device_id", "device_name", "ip_address", "device_type", "location", "usn" };

    bool isKnownField (const std::string &key)
    {
        for (const char *known : KNOWN_FIELDS)
        {
            if (key == known)
            {
                return (true);
            }
        }

        return (false);
    }

    // The fields of a zone this program writes; any others on it are kept as they were.
    const char *const KNOWN_ZONE_FIELDS[] = { "id", "name", "leader_id", "members" };

    bool isKnownZoneField (const std::string &key)
    {
        for (const char *known : KNOWN_ZONE_FIELDS)
        {
            if (key == known)
            {
                return (true);
            }
        }

        return (false);
    }

    std::string trimString (const std::string &input)
    {
        const auto start = input.find_first_not_of (" \t\r\n");

        if (start == std::string::npos)
        {
            return ("");
        }

        const auto end = input.find_last_not_of (" \t\r\n");

        return (input.substr (start, end - start + 1));
    }
}

SpeakerConfig::SpeakerConfig ()
{
}

bool SpeakerConfig::loadFromText (const std::string &text)
{
    m_speakers.clear ();
    m_defaultId.clear ();
    m_zones.clear ();
    m_defaultTarget = PlaybackTarget ();
    m_hasDefaultTarget = false;

    ordered document = ordered::parse (text, nullptr, false);

    if (document.is_discarded () || !document.is_object ())
    {
        return (false);
    }

    const auto list = document.find ("devices");

    if (list != document.end () && list->is_array ())
    {
        for (const ordered &item : *list)
        {
            if (!item.is_object ())
            {
                continue;
            }

            SoundTouchDevice speaker;

            speaker.deviceId = stringField (item, "device_id");
            speaker.deviceName = stringField (item, "device_name");
            speaker.ipAddress = stringField (item, "ip_address");
            speaker.deviceType = stringField (item, "device_type");
            speaker.location = stringField (item, "location");
            speaker.usn = stringField (item, "usn");

            // As DeviceDiscovery reads it: a speaker with no ID or no address cannot be driven.
            if (!speaker.deviceId.empty () && !speaker.ipAddress.empty () && findById (speaker.deviceId) == nullptr)
            {
                m_speakers.push_back (speaker);
            }
        }
    }

    m_defaultId = stringField (document, "default_device");

    const auto zoneList = document.find ("zones");

    if (zoneList != document.end () && zoneList->is_array ())
    {
        for (const ordered &item : *zoneList)
        {
            if (!item.is_object ())
            {
                continue;
            }

            ZoneConfig zone;

            zone.id = stringField (item, "id");
            zone.name = stringField (item, "name");
            zone.leaderId = stringField (item, "leader_id");

            const auto membersNode = item.find ("members");

            if (membersNode != item.end () && membersNode->is_array ())
            {
                for (const ordered &m : *membersNode)
                {
                    if (m.is_string ())
                    {
                        zone.memberIds.push_back (m.get<std::string> ());
                    }
                }
            }

            std::string valErr;
            if (!validateZone (zone, valErr))
            {
                // Any invalid zone definition or reference to unknown speakers makes the file invalid
                m_speakers.clear ();
                m_zones.clear ();
                m_defaultId.clear ();
                return (false);
            }

            m_zones.push_back (zone);
        }
    }

    const auto targetNode = document.find ("default_target");

    if (targetNode != document.end () && targetNode->is_object ())
    {
        const std::string typeStr = stringField (*targetNode, "type");
        const std::string idStr = stringField (*targetNode, "id");

        if (!typeStr.empty () && !idStr.empty ())
        {
            if (typeStr == "zone")
            {
                m_defaultTarget.type = PlaybackTarget::Type::Zone;
                m_defaultTarget.id = idStr;
                m_hasDefaultTarget = true;
            }
            else if (typeStr == "speaker")
            {
                m_defaultTarget.type = PlaybackTarget::Type::Speaker;
                m_defaultTarget.id = idStr;
                m_hasDefaultTarget = true;
            }
        }
    }

    if (!m_hasDefaultTarget && !m_defaultId.empty ())
    {
        m_defaultTarget.type = PlaybackTarget::Type::Speaker;
        m_defaultTarget.id = m_defaultId;
    }

    return (true);
}

const SoundTouchDevice *SpeakerConfig::getDefaultSpeaker () const
{
    if (m_hasDefaultTarget)
    {
        if (m_defaultTarget.type == PlaybackTarget::Type::Speaker)
        {
            const SoundTouchDevice *speaker = findById (m_defaultTarget.id);

            if (speaker != nullptr)
            {
                return (speaker);
            }
        }
        else if (m_defaultTarget.type == PlaybackTarget::Type::Zone)
        {
            const ZoneConfig *zone = findZoneById (m_defaultTarget.id);

            if (zone != nullptr)
            {
                const SoundTouchDevice *leader = findById (zone->leaderId);

                if (leader != nullptr)
                {
                    return (leader);
                }
            }
        }
    }

    if (!m_defaultId.empty ())
    {
        return (findById (m_defaultId));
    }

    return (m_speakers.empty () ? nullptr : &m_speakers.front ());
}

const SoundTouchDevice *SpeakerConfig::findById (const std::string &deviceId) const
{
    for (const SoundTouchDevice &speaker : m_speakers)
    {
        if (speaker.deviceId == deviceId)
        {
            return (&speaker);
        }
    }

    return (nullptr);
}

bool SpeakerConfig::remember (const SoundTouchDevice &speaker)
{
    if (speaker.deviceId.empty () || speaker.ipAddress.empty ())
    {
        return (false);
    }

    for (SoundTouchDevice &saved : m_speakers)
    {
        if (saved.deviceId == speaker.deviceId)
        {
            saved.ipAddress = speaker.ipAddress;

            // A look that did not get the speaker's name or model leaves the ones saved.
            if (!speaker.deviceName.empty ())
            {
                saved.deviceName = speaker.deviceName;
            }

            if (!speaker.deviceType.empty ())
            {
                saved.deviceType = speaker.deviceType;
            }

            if (!speaker.location.empty ())
            {
                saved.location = speaker.location;
            }

            if (!speaker.usn.empty ())
            {
                saved.usn = speaker.usn;
            }

            return (true);
        }
    }

    m_speakers.push_back (speaker);

    return (true);
}

bool SpeakerConfig::setDefault (const std::string &deviceId)
{
    if (findById (deviceId) == nullptr)
    {
        return (false);
    }

    m_defaultId = deviceId;
    m_defaultTarget.type = PlaybackTarget::Type::Speaker;
    m_defaultTarget.id = deviceId;
    m_hasDefaultTarget = true;

    return (true);
}

const ZoneConfig *SpeakerConfig::findZoneById (const std::string &zoneId) const
{
    for (const ZoneConfig &zone : m_zones)
    {
        if (zone.id == zoneId)
        {
            return (&zone);
        }
    }

    return (nullptr);
}

const ZoneConfig *SpeakerConfig::findZoneByName (const std::string &name) const
{
    for (const ZoneConfig &zone : m_zones)
    {
        if (zone.name == name)
        {
            return (&zone);
        }
    }

    return (nullptr);
}

bool SpeakerConfig::validateZone (const ZoneConfig &zone, std::string &error, const std::string &excludeZoneId) const
{
    const std::string trimmedId = trimString (zone.id);
    const std::string trimmedName = trimString (zone.name);

    if (trimmedId.empty ())
    {
        error = "Zone ID cannot be empty";
        return (false);
    }

    if (trimmedName.empty ())
    {
        error = "Zone name cannot be empty";
        return (false);
    }

    for (const ZoneConfig &existing : m_zones)
    {
        if (!excludeZoneId.empty () && existing.id == excludeZoneId)
        {
            continue;
        }

        if (existing.id == trimmedId)
        {
            error = "Zone ID '" + trimmedId + "' already exists";
            return (false);
        }

        if (existing.name == trimmedName)
        {
            error = "Zone name '" + trimmedName + "' already exists";
            return (false);
        }
    }

    if (zone.leaderId.empty ())
    {
        error = "Zone leader ID cannot be empty";
        return (false);
    }

    if (findById (zone.leaderId) == nullptr)
    {
        error = "Leader speaker '" + zone.leaderId + "' is not a known speaker";
        return (false);
    }

    if (zone.memberIds.size () < 2)
    {
        error = "Zone must have at least 2 distinct member speakers";
        return (false);
    }

    std::set<std::string> seen;
    bool leaderFound = false;

    for (const std::string &memberId : zone.memberIds)
    {
        if (memberId.empty ())
        {
            error = "Zone member ID cannot be empty";
            return (false);
        }

        if (findById (memberId) == nullptr)
        {
            error = "Member speaker '" + memberId + "' is not a known speaker";
            return (false);
        }

        if (!seen.insert (memberId).second)
        {
            error = "Duplicate speaker ID '" + memberId + "' in zone members";
            return (false);
        }

        if (memberId == zone.leaderId)
        {
            leaderFound = true;
        }
    }

    if (seen.size () < 2)
    {
        error = "Zone must contain at least 2 distinct speakers";
        return (false);
    }

    if (!leaderFound)
    {
        error = "Leader speaker '" + zone.leaderId + "' must be included in member list";
        return (false);
    }

    return (true);
}

bool SpeakerConfig::addZone (const ZoneConfig &zone, std::string &error)
{
    if (!validateZone (zone, error))
    {
        return (false);
    }

    ZoneConfig clean = zone;

    clean.id = trimString (zone.id);
    clean.name = trimString (zone.name);

    m_zones.push_back (clean);

    return (true);
}

bool SpeakerConfig::updateZone (const ZoneConfig &zone, std::string &error)
{
    const std::string trimmedId = trimString (zone.id);

    for (size_t i = 0; i < m_zones.size (); ++i)
    {
        if (m_zones[i].id == trimmedId)
        {
            if (!validateZone (zone, error, trimmedId))
            {
                return (false);
            }

            ZoneConfig clean = zone;

            clean.id = trimmedId;
            clean.name = trimString (zone.name);
            m_zones[i] = clean;

            return (true);
        }
    }

    error = "Zone ID '" + trimmedId + "' not found";

    return (false);
}

bool SpeakerConfig::removeZone (const std::string &zoneId, std::string &error)
{
    for (auto it = m_zones.begin (); it != m_zones.end (); ++it)
    {
        if (it->id == zoneId)
        {
            if (m_hasDefaultTarget && m_defaultTarget.type == PlaybackTarget::Type::Zone && m_defaultTarget.id == zoneId)
            {
                m_defaultTarget.type = PlaybackTarget::Type::Speaker;
                m_defaultTarget.id = m_defaultId;
                m_hasDefaultTarget = false;
            }

            m_zones.erase (it);

            return (true);
        }
    }

    error = "Zone ID '" + zoneId + "' not found";

    return (false);
}

bool SpeakerConfig::setDefaultTarget (const PlaybackTarget &target, std::string &error)
{
    if (target.type == PlaybackTarget::Type::Speaker)
    {
        const SoundTouchDevice *speaker = findById (target.id);

        if (speaker == nullptr)
        {
            error = "Speaker ID '" + target.id + "' not found";
            return (false);
        }

        m_defaultTarget = target;
        m_hasDefaultTarget = true;
        m_defaultId = target.id;

        return (true);
    }
    else if (target.type == PlaybackTarget::Type::Zone)
    {
        const ZoneConfig *zone = findZoneById (target.id);

        if (zone == nullptr)
        {
            error = "Zone ID '" + target.id + "' not found";
            return (false);
        }

        m_defaultTarget = target;
        m_hasDefaultTarget = true;
        m_defaultId = zone->leaderId;

        return (true);
    }

    error = "Invalid target type";

    return (false);
}

bool SpeakerConfig::resolveDefaultTarget (ResolvedTarget &out) const
{
    if (m_hasDefaultTarget)
    {
        std::string error;

        if (resolveTarget (m_defaultTarget, out, error))
        {
            return (true);
        }
    }

    const SoundTouchDevice *speaker = getDefaultSpeaker ();

    if (speaker == nullptr)
    {
        return (false);
    }

    out.leaderIp = speaker->ipAddress;
    out.leaderDeviceId = speaker->deviceId;
    out.leaderDeviceName = speaker->deviceName;
    out.isZone = false;
    out.autoResume = true;
    out.zoneId.clear ();
    out.zoneName.clear ();
    out.memberDeviceIds = { speaker->deviceId };
    out.memberIps = { speaker->ipAddress };

    return (true);
}

bool SpeakerConfig::resolveTarget (const PlaybackTarget &target, ResolvedTarget &out, std::string &error) const
{
    if (target.type == PlaybackTarget::Type::Speaker)
    {
        const SoundTouchDevice *speaker = findById (target.id);

        if (speaker == nullptr)
        {
            error = "Speaker ID '" + target.id + "' not found in configuration";
            return (false);
        }

        out.leaderIp = speaker->ipAddress;
        out.leaderDeviceId = speaker->deviceId;
        out.leaderDeviceName = speaker->deviceName;
        out.isZone = false;
        out.autoResume = true;
        out.zoneId.clear ();
        out.zoneName.clear ();
        out.memberDeviceIds = { speaker->deviceId };
        out.memberIps = { speaker->ipAddress };

        return (true);
    }
    else if (target.type == PlaybackTarget::Type::Zone)
    {
        const ZoneConfig *zone = findZoneById (target.id);

        if (zone == nullptr)
        {
            error = "Zone ID '" + target.id + "' not found in configuration";
            return (false);
        }

        const SoundTouchDevice *leader = findById (zone->leaderId);

        if (leader == nullptr)
        {
            error = "Leader speaker '" + zone->leaderId + "' for zone '" + zone->id + "' not found";
            return (false);
        }

        out.leaderIp = leader->ipAddress;
        out.leaderDeviceId = leader->deviceId;
        out.leaderDeviceName = leader->deviceName;
        out.isZone = true;
        out.autoResume = false; // per spec: zones require verification before auto-resume
        out.zoneId = zone->id;
        out.zoneName = zone->name;
        out.memberDeviceIds.clear ();
        out.memberIps.clear ();

        out.memberDeviceIds.push_back (leader->deviceId);
        out.memberIps.push_back (leader->ipAddress);

        for (const std::string &memberId : zone->memberIds)
        {
            if (memberId == zone->leaderId)
            {
                continue;
            }

            const SoundTouchDevice *memberSp = findById (memberId);

            if (memberSp == nullptr)
            {
                error = "Member speaker '" + memberId + "' for zone '" + zone->id + "' not found";
                return (false);
            }

            out.memberDeviceIds.push_back (memberSp->deviceId);
            out.memberIps.push_back (memberSp->ipAddress);
        }

        return (true);
    }

    error = "Unknown playback target type";

    return (false);
}

bool SpeakerConfig::parseTargetJson (const std::string &jsonStr, PlaybackTarget &target, std::string &error)
{
    ordered doc = ordered::parse (jsonStr, nullptr, false);

    if (doc.is_discarded () || !doc.is_object ())
    {
        error = "Invalid JSON object for playback target";
        return (false);
    }

    const std::string typeStr = stringField (doc, "type");
    const std::string idStr = stringField (doc, "id");

    if (typeStr.empty ())
    {
        error = "Missing 'type' in playback target";
        return (false);
    }

    if (idStr.empty ())
    {
        error = "Missing 'id' in playback target";
        return (false);
    }

    if (typeStr == "speaker")
    {
        target.type = PlaybackTarget::Type::Speaker;
        target.id = idStr;
        return (true);
    }
    else if (typeStr == "zone")
    {
        target.type = PlaybackTarget::Type::Zone;
        target.id = idStr;
        return (true);
    }

    error = "Invalid target type '" + typeStr + "' (must be 'speaker' or 'zone')";

    return (false);
}

std::string SpeakerConfig::targetToJson (const PlaybackTarget &target)
{
    ordered obj = ordered::object ();

    obj["type"] = (target.type == PlaybackTarget::Type::Zone ? "zone" : "speaker");
    obj["id"] = target.id;

    return (obj.dump ());
}

std::string SpeakerConfig::fileText (const std::string &existing) const
{
    ordered document = existing.empty () ? ordered::object () : ordered::parse (existing, nullptr, false);

    if (document.is_discarded () || !document.is_object ())
    {
        document = ordered::object ();
    }

    // What each saved speaker carries besides the fields written here, by its ID.
    std::map<std::string, ordered> extras;
    const auto oldList = document.find ("devices");

    if (oldList != document.end () && oldList->is_array ())
    {
        for (const ordered &item : *oldList)
        {
            const std::string id = item.is_object () ? stringField (item, "device_id") : std::string ();

            if (id.empty () || extras.count (id) != 0)
            {
                continue;
            }

            ordered kept = ordered::object ();

            for (auto field = item.begin (); field != item.end (); ++field)
            {
                if (!isKnownField (field.key ()))
                {
                    kept[field.key ()] = field.value ();
                }
            }

            extras.emplace (id, std::move (kept));
        }
    }

    // What each saved zone carries besides the fields written here, by its ID.
    std::map<std::string, ordered> zoneExtras;
    const auto oldZones = document.find ("zones");

    if (oldZones != document.end () && oldZones->is_array ())
    {
        for (const ordered &item : *oldZones)
        {
            const std::string id = item.is_object () ? stringField (item, "id") : std::string ();

            if (id.empty () || zoneExtras.count (id) != 0)
            {
                continue;
            }

            ordered kept = ordered::object ();

            for (auto field = item.begin (); field != item.end (); ++field)
            {
                if (!isKnownZoneField (field.key ()))
                {
                    kept[field.key ()] = field.value ();
                }
            }

            zoneExtras.emplace (id, std::move (kept));
        }
    }

    ordered list = ordered::array ();

    for (const SoundTouchDevice &speaker : m_speakers)
    {
        ordered item = ordered::object ();

        item["device_id"] = speaker.deviceId;
        item["device_name"] = speaker.deviceName;
        item["ip_address"] = speaker.ipAddress;

        if (!speaker.deviceType.empty ())
        {
            item["device_type"] = speaker.deviceType;
        }

        item["location"] = speaker.location;
        item["usn"] = speaker.usn;

        const auto kept = extras.find (speaker.deviceId);

        if (kept != extras.end ())
        {
            for (auto field = kept->second.begin (); field != kept->second.end (); ++field)
            {
                item[field.key ()] = field.value ();
            }
        }

        list.push_back (std::move (item));
    }

    // Legacy default_device value:
    std::string legacyDefault = m_defaultId;

    if (m_hasDefaultTarget)
    {
        if (m_defaultTarget.type == PlaybackTarget::Type::Speaker)
        {
            legacyDefault = m_defaultTarget.id;
        }
        else if (m_defaultTarget.type == PlaybackTarget::Type::Zone)
        {
            const ZoneConfig *zone = findZoneById (m_defaultTarget.id);

            if (zone != nullptr)
            {
                legacyDefault = zone->leaderId;
            }
        }
    }

    // Assigned in place, so each key keeps its position among the rest.
    document["default_device"] = legacyDefault;
    document["devices"] = std::move (list);

    // Save zones if they exist or existed in file:
    if (document.find ("zones") != document.end () || !m_zones.empty ())
    {
        ordered zoneList = ordered::array ();

        for (const ZoneConfig &zone : m_zones)
        {
            ordered item = ordered::object ();

            item["id"] = zone.id;
            item["name"] = zone.name;
            item["leader_id"] = zone.leaderId;

            ordered membersArr = ordered::array ();

            for (const std::string &memberId : zone.memberIds)
            {
                membersArr.push_back (memberId);
            }

            item["members"] = std::move (membersArr);

            const auto kept = zoneExtras.find (zone.id);

            if (kept != zoneExtras.end ())
            {
                for (auto field = kept->second.begin (); field != kept->second.end (); ++field)
                {
                    item[field.key ()] = field.value ();
                }
            }

            zoneList.push_back (std::move (item));
        }

        document["zones"] = std::move (zoneList);
    }

    // Save default_target if set or existed in file:
    if (m_hasDefaultTarget || document.find ("default_target") != document.end ())
    {
        ordered targetObj = ordered::object ();

        targetObj["type"] = (m_defaultTarget.type == PlaybackTarget::Type::Zone ? "zone" : "speaker");
        targetObj["id"] = m_defaultTarget.id;

        document["default_target"] = std::move (targetObj);
    }

    return (document.dump (2, ' ', false, ordered::error_handler_t::replace) + "\n");
}
