#include "WebJson.h"
#include <cstdint>
#include <initializer_list>

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

nlohmann::json WebJson::nowPlaying (const SoundTouchClient::NowPlaying &now, const std::string &station,
                                   const SoundTouchClient::Volume &volume, int stationPreset)
{
    nlohmann::json json;

    json["source"] = now.source;
    json["status"] = now.status;
    json["location"] = now.location;
    json["station"] = station;

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
