#include "WebJson.h"
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <map>

nlohmann::json WebJson::stream (const Stream &item)
{
    nlohmann::json json;

    json["name"] = item.name;
    json["display_name"] = item.displayName;
    json["description"] = item.description;
    json["url"] = item.url;

    // A stream with no preset reports null rather than 0, which is not a button.
    if (item.preset != 0)
    {
        json["preset"] = item.preset;
    }
    else
    {
        json["preset"] = nullptr;
    }

    return (json);
}

nlohmann::json WebJson::streams (const std::vector<Stream> &items)
{
    nlohmann::json array = nlohmann::json::array ();

    for (const Stream &item : items)
    {
        array.push_back (stream (item));
    }

    return (array);
}

nlohmann::json WebJson::device (const SoundTouchDevice &item, bool isDefault)
{
    nlohmann::json json;

    json["device_id"] = item.deviceId;
    json["device_name"] = item.deviceName;
    json["ip_address"] = item.ipAddress;
    json["is_default"] = isDefault;

    return (json);
}

nlohmann::json WebJson::devices (const std::vector<SoundTouchDevice> &items, const std::string &defaultDeviceId)
{
    nlohmann::json array = nlohmann::json::array ();

    for (const SoundTouchDevice &item : items)
    {
        array.push_back (device (item, !defaultDeviceId.empty () && item.deviceId == defaultDeviceId));
    }

    nlohmann::json json;

    json["default_device"] = defaultDeviceId;
    json["devices"] = array;

    return (json);
}

nlohmann::json WebJson::speakers (const SpeakerConfig &saved, const std::vector<SeenSpeaker> &seen,
                                  const SpeakerSearch &search, long long onlineMs, const std::string &activeIp)
{
    const SoundTouchDevice *defaultSpeaker = saved.getDefaultSpeaker ();
    const std::string defaultId = (defaultSpeaker != nullptr) ? defaultSpeaker->deviceId : std::string ();

    auto seenOf = [&seen] (const std::string &id) -> const SeenSpeaker *
    {
        for (const SeenSpeaker &item : seen)
        {
            if (!id.empty () && item.device.deviceId == id)
            {
                return (&item);
            }
        }

        return (nullptr);
    };

    auto entry = [&] (const SoundTouchDevice *stored, const SeenSpeaker *found)
    {
        const SoundTouchDevice &device = (found != nullptr) ? found->device : *stored;
        nlohmann::json item;

        item["id"] = device.deviceId;
        item["name"] = (found != nullptr && found->device.deviceName.empty () && stored != nullptr)
                           ? stored->deviceName : device.deviceName;
        item["ip"] = device.ipAddress;
        item["type"] = (found != nullptr && found->device.deviceType.empty () && stored != nullptr)
                           ? stored->deviceType : device.deviceType;
        item["saved"] = (stored != nullptr);
        item["default"] = (stored != nullptr && !defaultId.empty () && stored->deviceId == defaultId);
        item["active"] = (!activeIp.empty () && stored != nullptr && stored->ipAddress == activeIp);
        item["last_seen_ms"] = (found != nullptr) ? nlohmann::json (found->agoMs) : nlohmann::json (nullptr);

        if (found != nullptr)
        {
            item["online"] = (found->agoMs <= onlineMs);
        }
        else
        {
            item["online"] = (search.passes > 0) ? nlohmann::json (false) : nlohmann::json (nullptr);
        }

        // A saved speaker found at another address: making it the default again saves the new one.
        if (stored != nullptr && found != nullptr && stored->ipAddress != found->device.ipAddress)
        {
            item["saved_ip"] = stored->ipAddress;
        }

        return (item);
    };

    nlohmann::json list = nlohmann::json::array ();

    if (defaultSpeaker != nullptr)
    {
        list.push_back (entry (defaultSpeaker, seenOf (defaultSpeaker->deviceId)));
    }

    for (const SoundTouchDevice &stored : saved.getSpeakers ())
    {
        if (&stored != defaultSpeaker)
        {
            list.push_back (entry (&stored, seenOf (stored.deviceId)));
        }
    }

    std::vector<const SeenSpeaker *> others;

    for (const SeenSpeaker &item : seen)
    {
        if (item.device.deviceId.empty () || saved.findById (item.device.deviceId) == nullptr)
        {
            others.push_back (&item);
        }
    }

    // By name, those without one last, then by address.
    std::stable_sort (others.begin (), others.end (), [] (const SeenSpeaker *a, const SeenSpeaker *b)
    {
        const std::string &left = a->device.deviceName;
        const std::string &right = b->device.deviceName;

        if (left.empty () != right.empty ())
        {
            return (right.empty ());
        }

        return ((left != right) ? left < right : a->device.ipAddress < b->device.ipAddress);
    });

    for (const SeenSpeaker *item : others)
    {
        list.push_back (entry (nullptr, item));
    }

    nlohmann::json json;

    json["default_id"] = defaultId;
    json["has_default"] = !defaultId.empty ();
    json["embedded"] = !activeIp.empty ();
    json["active_ip"] = activeIp;
    json["speakers"] = list;
    json["groups"] = nlohmann::json::array ();
    json["discovery"] = {
        { "active", search.active },
        { "searching", search.searching },
        { "passes", search.passes },
        { "last_ms", (search.lastAgoMs < 0) ? nlohmann::json (nullptr) : nlohmann::json (search.lastAgoMs) }
    };

    return (json);
}

bool WebJson::parseSpeakerId (const std::string &body, std::string &id)
{
    const nlohmann::json json = nlohmann::json::parse (body, nullptr, false);

    if (!json.is_object ())
    {
        return (false);
    }

    const auto found = json.find ("id");

    if (found == json.end () || !found->is_string ())
    {
        return (false);
    }

    const std::string value = found->get<std::string> ();

    if (value.empty () || value.size () > 64)
    {
        return (false);
    }

    id = value;

    return (true);
}

nlohmann::json WebJson::nowPlaying (const SoundTouchClient::NowPlaying &now, const std::string &station,
                                   const SoundTouchClient::Volume &volume, int stationPreset,
                                   const std::string &stationName)
{
    nlohmann::json json;

    json["source"] = now.source;
    json["status"] = now.status;
    json["location"] = now.location;
    json["station"] = station;
    json["station_name"] = stationName;

    // Null for a station on no preset (or none matched), which no preset button stands for.
    json["station_preset"] = (stationPreset > 0) ? nlohmann::json (stationPreset) : nlohmann::json (nullptr);

    // Null when the speaker could not be asked, so the dashboard shows nothing rather than 0.
    if (volume.valid)
    {
        json["volume"] = volume.actual;
        json["muted"] = volume.muted;
    }
    else
    {
        json["volume"] = nullptr;
        json["muted"] = nullptr;
    }

    return (json);
}

namespace
{
    // The body as a JSON object; false, not throwing, when it is not one: a malformed body is the
    // caller's mistake, answered with a 400.
    bool asObject (const std::string &body, nlohmann::json &json)
    {
        json = nlohmann::json::parse (body, nullptr, false);

        return (!json.is_discarded () && json.is_object ());
    }

    // A whole number from low to high under key, set into value only when it is one. Read as unsigned
    // too, so that a huge positive number is not taken for a small or negative one.
    bool wholeIn (const std::string &body, const char *key, std::int64_t low, std::int64_t high, std::int64_t &value)
    {
        nlohmann::json json;

        if (!asObject (body, json))
        {
            return (false);
        }

        const auto found = json.find (key);

        if (found == json.end () || !found->is_number_integer ())
        {
            return (false);
        }

        std::int64_t got = 0;

        if (found->is_number_unsigned ())
        {
            const std::uint64_t positive = found->get<std::uint64_t> ();

            if (high < 0 || positive > static_cast<std::uint64_t> (high))
            {
                return (false);
            }

            got = static_cast<std::int64_t> (positive);
        }
        else
        {
            got = found->get<std::int64_t> ();
        }

        if (got < low || got > high)
        {
            return (false);
        }

        value = got;

        return (true);
    }

    // One of the allowed words under key, set into value only when it is one.
    bool oneOf (const std::string &body, const char *key, std::initializer_list<const char *> allowed, std::string &value)
    {
        nlohmann::json json;

        if (!asObject (body, json))
        {
            return (false);
        }

        const auto found = json.find (key);

        if (found == json.end () || !found->is_string ())
        {
            return (false);
        }

        const std::string got = found->get<std::string> ();

        for (const char *word : allowed)
        {
            if (got == word)
            {
                value = got;
                return (true);
            }
        }

        return (false);
    }
}

bool WebJson::parseVolume (const std::string &body, int &level)
{
    std::int64_t value = 0;

    if (!wholeIn (body, "volume", 0, 100, value))
    {
        return (false);
    }

    level = static_cast<int> (value);

    return (true);
}

bool WebJson::parsePlayback (const std::string &body, std::string &action)
{
    return (oneOf (body, "action", { "play", "pause" }, action));
}

bool WebJson::parsePower (const std::string &body, bool &on)
{
    nlohmann::json json;

    if (!asObject (body, json))
    {
        return (false);
    }

    const auto found = json.find ("on");

    if (found == json.end () || !found->is_boolean ())
    {
        return (false);
    }

    on = found->get<bool> ();

    return (true);
}

bool WebJson::parsePreset (const std::string &body, int &preset)
{
    std::int64_t value = 0;

    if (!wholeIn (body, "preset", 1, 6, value))
    {
        return (false);
    }

    preset = static_cast<int> (value);

    return (true);
}

bool WebJson::parseSkip (const std::string &body, std::string &direction)
{
    return (oneOf (body, "direction", { "next", "previous" }, direction));
}

bool WebJson::parseSource (const std::string &body, std::string &source)
{
    return (oneOf (body, "source", { "bluetooth", "aux" }, source));
}

bool WebJson::parseSelect (const std::string &body, int &preset)
{
    std::int64_t value = 0;

    if (!wholeIn (body, "preset", 1, 666, value))
    {
        return (false);
    }

    preset = static_cast<int> (value);

    return (true);
}

bool WebJson::buttonsOf (int preset, std::vector<int> &buttons)
{
    if (preset < 1 || preset > 666)
    {
        return (false);
    }

    std::vector<int> digits;

    for (int value = preset; value != 0; value /= 10)
    {
        const int digit = value % 10;

        if (digit < 1 || digit > 6)
        {
            return (false);
        }

        digits.insert (digits.begin (), digit);
    }

    buttons = digits;

    return (true);
}

bool WebJson::parsePlayStream (const std::string &body, std::string &name)
{
    nlohmann::json json;

    if (!asObject (body, json))
    {
        return (false);
    }

    const auto found = json.find ("stream");

    if (found == json.end () || !found->is_string () || !StreamConfig::isValidName (found->get<std::string> ()))
    {
        return (false);
    }

    name = found->get<std::string> ();

    return (true);
}

namespace
{
    std::string trimmed (const std::string &text)
    {
        const char *space = " \t\r\n\f\v";
        const size_t first = text.find_first_not_of (space);

        if (first == std::string::npos)
        {
            return (std::string ());
        }

        return (text.substr (first, text.find_last_not_of (space) - first + 1));
    }

    bool hasControlCharacter (const std::string &text)
    {
        for (const char c : text)
        {
            const unsigned char byte = static_cast<unsigned char> (c);

            if (byte < 0x20 || byte == 0x7F)
            {
                return (true);
            }
        }

        return (false);
    }
}

bool WebJson::parseStreams (const std::string &body, std::vector<Stream> &streams, nlohmann::json &problems)
{
    problems = nlohmann::json::array ();

    nlohmann::json json;
    const auto list = asObject (body, json) ? json.find ("streams") : json.end ();

    if (list == json.end () || !list->is_array ())
    {
        problems.push_back ({ { "error", "expected {\"streams\": [...]}" } });
        return (false);
    }

    std::vector<Stream> parsed;
    std::map<std::string, std::vector<size_t>> byName;
    std::map<int, std::vector<size_t>> byPreset;

    auto problem = [&problems] (size_t index, const char *field, const std::string &error)
    {
        problems.push_back ({ { "index", index }, { "field", field }, { "error", error } });
    };

    for (size_t index = 0; index < list->size (); ++index)
    {
        const nlohmann::json &item = (*list)[index];
        Stream stream;

        if (!item.is_object ())
        {
            problem (index, "", "each stream is an object");
            continue;
        }

        // A text field, trimmed: "" when it is absent or null. False, saying why, when it cannot be used.
        auto text = [&] (const char *field, size_t limit, std::string &value)
        {
            const auto found = item.find (field);

            if (found == item.end () || found->is_null ())
            {
                return (true);
            }

            if (!found->is_string ())
            {
                problem (index, field, "must be text");
                return (false);
            }

            value = trimmed (found->get<std::string> ());

            if (value.size () > limit)
            {
                problem (index, field, "is longer than " + std::to_string (limit) + " characters");
                return (false);
            }

            if (hasControlCharacter (value))
            {
                problem (index, field, "has a control character in it");
                return (false);
            }

            return (true);
        };

        if (text ("name", 1024, stream.name))
        {
            if (stream.name.empty ())
            {
                problem (index, "name", "a name is needed");
            }
            else if (!StreamConfig::isValidName (stream.name))
            {
                problem (index, "name", "use letters, digits, '.', '_' and '-' only, up to "
                                        + std::to_string (StreamConfig::MAX_NAME));
            }
            else
            {
                byName[stream.name].push_back (index);
            }
        }

        text ("display_name", StreamConfig::MAX_DISPLAY_NAME, stream.displayName);
        text ("description", StreamConfig::MAX_DESCRIPTION, stream.description);

        if (text ("url", StreamConfig::MAX_URL, stream.url))
        {
            if (stream.url.empty ())
            {
                problem (index, "url", "a URL is needed");
            }
            else if (!StreamConfig::isValidUrl (stream.url))
            {
                problem (index, "url", "must be an http:// or https:// address, with no spaces in it");
            }
        }

        const auto preset = item.find ("preset");

        if (preset != item.end () && !preset->is_null ())
        {
            std::int64_t number = -1;

            if (preset->is_number_unsigned ())
            {
                number = static_cast<std::int64_t> (std::min<std::uint64_t> (preset->get<std::uint64_t> (), 1000));
            }
            else if (preset->is_number_integer ())
            {
                number = preset->get<std::int64_t> ();
            }

            if (number == 0)
            {
                stream.preset = 0;
            }
            else if (number < 0 || number > 666 || !StreamConfig::isValidPreset (static_cast<int> (number)))
            {
                problem (index, "preset", "presets are made of the buttons 1 to 6, up to three of them, such as 1, 13 or 111");
            }
            else
            {
                stream.preset = static_cast<int> (number);
                byPreset[stream.preset].push_back (index);
            }
        }

        if (stream.displayName.empty ())
        {
            stream.displayName = stream.name;
        }

        parsed.push_back (stream);
    }

    for (const auto &[name, indexes] : byName)
    {
        for (size_t i = 0; indexes.size () > 1 && i < indexes.size (); ++i)
        {
            problem (indexes[i], "name", "another stream is called " + name);
        }
    }

    for (const auto &[preset, indexes] : byPreset)
    {
        for (size_t i = 0; indexes.size () > 1 && i < indexes.size (); ++i)
        {
            problem (indexes[i], "preset", "another stream is on preset " + std::to_string (preset));
        }
    }

    if (!problems.empty ())
    {
        return (false);
    }

    streams = std::move (parsed);

    return (true);
}
