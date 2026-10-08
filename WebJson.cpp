#include "WebJson.h"
#include <cstdint>

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
                                   const SoundTouchClient::Volume &volume)
{
    nlohmann::json json;

    json["source"] = now.source;
    json["status"] = now.status;
    json["location"] = now.location;
    json["station"] = station;

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

bool WebJson::parseVolume (const std::string &body, int &level)
{
    // Not throwing: a malformed body is the caller's mistake, answered with a 400.
    const nlohmann::json json = nlohmann::json::parse (body, nullptr, false);

    if (json.is_discarded () || !json.is_object ())
    {
        return (false);
    }

    const auto found = json.find ("volume");

    if (found == json.end () || !found->is_number_integer ())
    {
        return (false);
    }

    // Read as unsigned too, so a huge positive value is not taken for a small or negative one.
    if (found->is_number_unsigned ())
    {
        const std::uint64_t value = found->get<std::uint64_t> ();

        if (value > 100)
        {
            return (false);
        }

        level = static_cast<int> (value);

        return (true);
    }

    const std::int64_t value = found->get<std::int64_t> ();

    if (value < 0 || value > 100)
    {
        return (false);
    }

    level = static_cast<int> (value);

    return (true);
}

bool WebJson::parsePlayback (const std::string &body, std::string &action)
{
    const nlohmann::json json = nlohmann::json::parse (body, nullptr, false);

    if (json.is_discarded () || !json.is_object ())
    {
        return (false);
    }

    const auto found = json.find ("action");

    if (found == json.end () || !found->is_string ())
    {
        return (false);
    }

    const std::string value = found->get<std::string> ();

    if (value != "play" && value != "pause")
    {
        return (false);
    }

    action = value;

    return (true);
}

bool WebJson::parsePower (const std::string &body, bool &on)
{
    const nlohmann::json json = nlohmann::json::parse (body, nullptr, false);

    if (json.is_discarded () || !json.is_object ())
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
