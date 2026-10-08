#ifndef WEB_JSON_H
#define WEB_JSON_H

#include "StreamConfig.h"
#include "DeviceDiscovery.h"
#include "SoundTouchClient.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Serialisers from the program's plain data structures to the JSON the web API returns. Deliberately
// free of sockets and of the loader/client machinery, so they can be unit-tested on hand-built
// structs without a network or a speaker.
namespace WebJson
{
    nlohmann::json stream (const Stream &item);
    nlohmann::json streams (const std::vector<Stream> &items);

    nlohmann::json device (const SoundTouchDevice &item, bool isDefault);
    nlohmann::json devices (const std::vector<SoundTouchDevice> &items, const std::string &defaultDeviceId);

    // station is the resolved display/name of what is playing, or "" when it could not be matched.
    // volume is the speaker's level; when it is not valid the volume/muted fields report null.
    nlohmann::json nowPlaying (const SoundTouchClient::NowPlaying &now, const std::string &station,
                               const SoundTouchClient::Volume &volume = SoundTouchClient::Volume ());

    // Reads a request to set the volume, {"volume": N} with N a whole number from 0 to 100. False,
    // leaving level alone, for anything else.
    bool parseVolume (const std::string &body, int &level);

    // Reads a request to play or pause, {"action": "play"} or {"action": "pause"}. False, leaving
    // action alone, for anything else.
    bool parsePlayback (const std::string &body, std::string &action);

    // Reads a request to switch the speaker on or off, {"on": true} or {"on": false}. False, leaving
    // on alone, for anything else.
    bool parsePower (const std::string &body, bool &on);
}

#endif
