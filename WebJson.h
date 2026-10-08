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

    // station is the resolved display/name of what is playing, or "" when it could not be matched, and
    // stationPreset its preset (0 for none). volume is the speaker's level; when it is not valid the
    // volume/muted fields report null.
    nlohmann::json nowPlaying (const SoundTouchClient::NowPlaying &now, const std::string &station,
                               const SoundTouchClient::Volume &volume = SoundTouchClient::Volume (),
                               int stationPreset = 0);

    // Reads a request to set the volume, {"volume": N} with N a whole number from 0 to 100. False,
    // leaving level alone, for anything else.
    bool parseVolume (const std::string &body, int &level);

    // Reads a request to play or pause, {"action": "play"} or {"action": "pause"}. False, leaving
    // action alone, for anything else.
    bool parsePlayback (const std::string &body, std::string &action);

    // Reads a request to switch the speaker on or off, {"on": true} or {"on": false}. False, leaving
    // on alone, for anything else.
    bool parsePower (const std::string &body, bool &on);

    // Reads a request to press a preset button, {"preset": N} with N from 1 to 6. False, leaving
    // preset alone, for anything else.
    bool parsePreset (const std::string &body, int &preset);

    // Reads a request to skip, {"direction": "next"} or {"direction": "previous"}. False, leaving
    // direction alone, for anything else.
    bool parseSkip (const std::string &body, std::string &direction);

    // Reads a request to switch source, {"source": "bluetooth"} or {"source": "aux"}. False, leaving
    // source alone, for anything else.
    bool parseSource (const std::string &body, std::string &source);

    // Reads a request to play a preset by its number, {"preset": N} with N a whole number up to 666.
    // False, leaving preset alone, for anything else; whether N is made of buttons is buttonsOf's to say.
    bool parseSelect (const std::string &body, int &preset);

    // The buttons that spell out a preset, as the remote presses them: up to three, each 1 to 6, so
    // preset 111 is 1, 1, 1. False, leaving buttons alone, for a number not made that way.
    bool buttonsOf (int preset, std::vector<int> &buttons);
}

#endif
