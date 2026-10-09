#include "SpeakerConfig.h"
#include <map>
#include <utility>
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
}

SpeakerConfig::SpeakerConfig ()
{
}

bool SpeakerConfig::loadFromText (const std::string &text)
{
    m_speakers.clear ();
    m_defaultId.clear ();

    ordered document = ordered::parse (text, nullptr, false);

    if (document.is_discarded () || !document.is_object ())
    {
        return (false);
    }

    std::vector<SoundTouchDevice> speakers;
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

    return (true);
}

const SoundTouchDevice *SpeakerConfig::getDefaultSpeaker () const
{
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

    return (true);
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

    // Assigned in place, so each key keeps its position among the rest.
    document["default_device"] = m_defaultId;
    document["devices"] = std::move (list);

    return (document.dump (2, ' ', false, ordered::error_handler_t::replace) + "\n");
}
