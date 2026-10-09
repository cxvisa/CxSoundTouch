// A small, dependency-free unit test for the web server's pure logic: HTTP request-line parsing, JSON
// serialisation, the stream list's rules and its file, and the live-change signal. It links only
// HttpUtil, WebJson, StreamConfig and SpeakerConfig (no sockets, curl or pugixml; LiveSignal is header-only), so it
// builds and runs anywhere the main program does. Run with: make test
#include "HttpUtil.h"
#include "WebJson.h"
#include "LiveSignal.h"
#include "StreamConfig.h"
#include "SpeakerConfig.h"
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

static int g_checks = 0;
static int g_failures = 0;

static void check (bool condition, const char *expression, const char *file, int line)
{
    ++g_checks;

    if (!condition)
    {
        ++g_failures;
        std::cerr << "FAIL " << file << ":" << line << ": " << expression << "\n";
    }
}

#define CHECK(cond) check ((cond), #cond, __FILE__, __LINE__)

static void testFirstLine ()
{
    CHECK (HttpUtil::firstLine ("GET / HTTP/1.1\r\nHost: x\r\n\r\n") == "GET / HTTP/1.1");
    CHECK (HttpUtil::firstLine ("GET / HTTP/1.1\nHost: x") == "GET / HTTP/1.1");
    CHECK (HttpUtil::firstLine ("no newline here") == "no newline here");
    CHECK (HttpUtil::firstLine ("") == "");
}

static void testParseRequestLine ()
{
    HttpRequest request;

    CHECK (HttpUtil::parseRequestLine ("GET /api/streams HTTP/1.1", request));
    CHECK (request.method == "GET");
    CHECK (request.path == "/api/streams");
    CHECK (request.query == "");
    CHECK (request.version == "HTTP/1.1");

    HttpRequest withQuery;

    CHECK (HttpUtil::parseRequestLine ("GET /api/songs?station=klove&x=1 HTTP/1.1", withQuery));
    CHECK (withQuery.path == "/api/songs");
    CHECK (withQuery.query == "station=klove&x=1");

    HttpRequest head;

    CHECK (HttpUtil::parseRequestLine ("HEAD / HTTP/1.0", head));
    CHECK (head.method == "HEAD");
    CHECK (head.path == "/");

    HttpRequest bad;

    CHECK (!HttpUtil::parseRequestLine ("", bad));
    CHECK (!HttpUtil::parseRequestLine ("GET /only-two", bad));
    CHECK (!HttpUtil::parseRequestLine ("GET relative HTTP/1.1", bad));       // no leading '/'
    CHECK (!HttpUtil::parseRequestLine ("GET /x FTP/1.1", bad));              // not HTTP
    CHECK (!HttpUtil::parseRequestLine ("GET /x HTTP/1.1 extra", bad));       // a fourth token
}

static void testPercentDecode ()
{
    CHECK (HttpUtil::percentDecode ("a%20b") == "a b");
    CHECK (HttpUtil::percentDecode ("a+b") == "a b");
    CHECK (HttpUtil::percentDecode ("%2Fpath") == "/path");
    CHECK (HttpUtil::percentDecode ("100%") == "100%");          // malformed, left as written
    CHECK (HttpUtil::percentDecode ("x%zzy") == "x%zzy");        // malformed, left as written
}

static void testQueryValue ()
{
    CHECK (HttpUtil::queryValue ("station=klove&x=1", "station") == "klove");
    CHECK (HttpUtil::queryValue ("station=klove&x=1", "x") == "1");
    CHECK (HttpUtil::queryValue ("station=klove", "missing") == "");
    CHECK (HttpUtil::queryValue ("a=b%20c", "a") == "b c");
    CHECK (HttpUtil::queryValue ("", "a") == "");
}

static void testParseUnsigned ()
{
    std::uint64_t value = 7;

    CHECK (HttpUtil::parseUnsigned ("0", value) && value == 0);
    CHECK (HttpUtil::parseUnsigned ("42", value) && value == 42);
    CHECK (HttpUtil::parseUnsigned ("18446744073709551615", value) && value == 18446744073709551615ULL);

    value = 7;

    CHECK (!HttpUtil::parseUnsigned ("", value));
    CHECK (!HttpUtil::parseUnsigned ("-1", value));
    CHECK (!HttpUtil::parseUnsigned ("+1", value));
    CHECK (!HttpUtil::parseUnsigned (" 1", value));
    CHECK (!HttpUtil::parseUnsigned ("12a", value));
    CHECK (!HttpUtil::parseUnsigned ("18446744073709551616", value));     // one past 64 bits
    CHECK (value == 7);                                                  // left alone on failure
}

static void testHeaderValue ()
{
    const std::string head = "POST /api/volume HTTP/1.1\r\n"
                             "Host: speaker.local:8081\r\n"
                             "content-type:   Application/JSON; charset=utf-8  \r\n"
                             "X-Content-Length: 99\r\n"
                             "Content-Length: 14";

    CHECK (HttpUtil::headerValue (head, "Content-Type") == "Application/JSON; charset=utf-8");
    CHECK (HttpUtil::headerValue (head, "CONTENT-LENGTH") == "14");       // not X-Content-Length's
    CHECK (HttpUtil::headerValue (head, "Host") == "speaker.local:8081"); // a colon in the value
    CHECK (HttpUtil::headerValue (head, "Accept") == "");
    CHECK (HttpUtil::headerValue (head, "POST /api/volume HTTP/1.1") == ""); // the request line is no header
    CHECK (HttpUtil::headerValue ("GET / HTTP/1.1\nHost: lf-only", "host") == "lf-only");
}

static void testMediaType ()
{
    CHECK (HttpUtil::mediaType ("application/json") == "application/json");
    CHECK (HttpUtil::mediaType ("Application/JSON; charset=utf-8") == "application/json");
    CHECK (HttpUtil::mediaType ("  text/plain ;charset=UTF-8") == "text/plain");
    CHECK (HttpUtil::mediaType ("") == "");
    CHECK (HttpUtil::mediaType ("application/jsonx") != "application/json");
}

static void testParseVolume ()
{
    int level = -1;

    CHECK (WebJson::parseVolume ("{\"volume\": 0}", level) && level == 0);
    CHECK (WebJson::parseVolume ("{\"volume\": 100}", level) && level == 100);
    CHECK (WebJson::parseVolume ("{\"volume\":42,\"other\":true}", level) && level == 42);

    level = -1;

    CHECK (!WebJson::parseVolume ("{\"volume\": 101}", level));
    CHECK (!WebJson::parseVolume ("{\"volume\": -1}", level));
    CHECK (!WebJson::parseVolume ("{\"volume\": 18446744073709551615}", level));   // not wrapped to -1
    CHECK (!WebJson::parseVolume ("{\"volume\": 42.5}", level));
    CHECK (!WebJson::parseVolume ("{\"volume\": \"42\"}", level));
    CHECK (!WebJson::parseVolume ("{\"level\": 42}", level));
    CHECK (!WebJson::parseVolume ("[42]", level));
    CHECK (!WebJson::parseVolume ("{\"volume\": ", level));
    CHECK (!WebJson::parseVolume ("", level));
    CHECK (level == -1);                                                 // left alone on failure
}

static void testParsePlayback ()
{
    std::string action = "unset";

    CHECK (WebJson::parsePlayback ("{\"action\": \"play\"}", action) && action == "play");
    CHECK (WebJson::parsePlayback ("{\"action\":\"pause\",\"other\":1}", action) && action == "pause");

    action = "unset";

    CHECK (!WebJson::parsePlayback ("{\"action\": \"stop\"}", action));
    CHECK (!WebJson::parsePlayback ("{\"action\": \"PLAY\"}", action));       // the API's names, not the keys'
    CHECK (!WebJson::parsePlayback ("{\"action\": true}", action));
    CHECK (!WebJson::parsePlayback ("{}", action));
    CHECK (!WebJson::parsePlayback ("[\"play\"]", action));
    CHECK (!WebJson::parsePlayback ("{\"action\": ", action));
    CHECK (action == "unset");                                           // left alone on failure
}

static void testParsePower ()
{
    bool on = false;

    CHECK (WebJson::parsePower ("{\"on\": true}", on) && on);
    CHECK (WebJson::parsePower ("{\"on\": false}", on) && !on);

    on = true;

    CHECK (!WebJson::parsePower ("{\"on\": 1}", on));                     // a boolean, not a number
    CHECK (!WebJson::parsePower ("{\"on\": \"false\"}", on));
    CHECK (!WebJson::parsePower ("{\"power\": false}", on));
    CHECK (!WebJson::parsePower ("", on));
    CHECK (on);                                                          // left alone on failure
}

static void testParsePreset ()
{
    int preset = -1;

    CHECK (WebJson::parsePreset ("{\"preset\": 1}", preset) && preset == 1);
    CHECK (WebJson::parsePreset ("{\"preset\": 6}", preset) && preset == 6);

    preset = -1;

    CHECK (!WebJson::parsePreset ("{\"preset\": 0}", preset));
    CHECK (!WebJson::parsePreset ("{\"preset\": 7}", preset));            // the remote has six
    CHECK (!WebJson::parsePreset ("{\"preset\": 11}", preset));           // a combo is pressed as two
    CHECK (!WebJson::parsePreset ("{\"preset\": \"1\"}", preset));
    CHECK (!WebJson::parsePreset ("{\"preset\": 18446744073709551617}", preset));
    CHECK (preset == -1);                                                // left alone on failure
}

static void testParseSkipAndSource ()
{
    std::string value = "unset";

    CHECK (WebJson::parseSkip ("{\"direction\": \"next\"}", value) && value == "next");
    CHECK (WebJson::parseSkip ("{\"direction\": \"previous\"}", value) && value == "previous");
    CHECK (WebJson::parseSource ("{\"source\": \"bluetooth\"}", value) && value == "bluetooth");
    CHECK (WebJson::parseSource ("{\"source\": \"aux\"}", value) && value == "aux");

    value = "unset";

    CHECK (!WebJson::parseSkip ("{\"direction\": \"prev\"}", value));
    CHECK (!WebJson::parseSkip ("{\"direction\": \"NEXT_TRACK\"}", value));  // the API's names, not the keys'
    CHECK (!WebJson::parseSource ("{\"source\": \"BLUETOOTH\"}", value));
    CHECK (!WebJson::parseSource ("{\"source\": \"upnp\"}", value));         // only the speaker's own inputs
    CHECK (!WebJson::parseSource ("{\"direction\": \"aux\"}", value));
    CHECK (value == "unset");                                            // left alone on failure
}

static void testSelectAndButtons ()
{
    int preset = -1;

    CHECK (WebJson::parseSelect ("{\"preset\": 111}", preset) && preset == 111);
    CHECK (WebJson::parseSelect ("{\"preset\": 1}", preset) && preset == 1);

    preset = -1;

    CHECK (!WebJson::parseSelect ("{\"preset\": 0}", preset));
    CHECK (!WebJson::parseSelect ("{\"preset\": 667}", preset));
    CHECK (!WebJson::parseSelect ("{\"preset\": \"111\"}", preset));
    CHECK (preset == -1);

    std::vector<int> buttons { 9 };

    CHECK (WebJson::buttonsOf (1, buttons) && buttons == std::vector<int> ({ 1 }));
    CHECK (WebJson::buttonsOf (13, buttons) && buttons == std::vector<int> ({ 1, 3 }));
    CHECK (WebJson::buttonsOf (111, buttons) && buttons == std::vector<int> ({ 1, 1, 1 }));
    CHECK (WebJson::buttonsOf (666, buttons) && buttons == std::vector<int> ({ 6, 6, 6 }));

    buttons = { 9 };

    CHECK (!WebJson::buttonsOf (7, buttons));                            // no button 7
    CHECK (!WebJson::buttonsOf (10, buttons));                           // nor 0
    CHECK (!WebJson::buttonsOf (17, buttons));
    CHECK (!WebJson::buttonsOf (601, buttons));
    CHECK (!WebJson::buttonsOf (1111, buttons));                         // three buttons at most
    CHECK (!WebJson::buttonsOf (0, buttons));
    CHECK (!WebJson::buttonsOf (-11, buttons));
    CHECK (buttons == std::vector<int> ({ 9 }));                         // left alone on failure
}

static long long millisecondsSince (std::chrono::steady_clock::time_point start)
{
    return (std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - start).count ());
}

static void testLiveSignal ()
{
    LiveSignal signal;
    std::atomic<bool> abandon (false);

    CHECK (signal.seq () == 0);

    signal.notify ();

    CHECK (signal.seq () == 1);

    // Already past what the caller saw: no wait at all.
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now ();

    CHECK (signal.waitPast (0, std::chrono::milliseconds (5000), abandon) == 1);
    CHECK (millisecondsSince (start) < 1000);

    // Nothing changes: it waits out the timeout and reports the same sequence.
    start = std::chrono::steady_clock::now ();

    CHECK (signal.waitPast (1, std::chrono::milliseconds (150), abandon) == 1);
    CHECK (millisecondsSince (start) >= 140);

    // A change from another thread ends the wait early, with the new sequence.
    std::thread changer ([&signal] ()
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
        signal.notify ();
    });

    start = std::chrono::steady_clock::now ();

    CHECK (signal.waitPast (1, std::chrono::milliseconds (5000), abandon) == 2);
    CHECK (millisecondsSince (start) < 2500);

    changer.join ();

    // Abandoned, as by a server stopping, it ends early with no change.
    std::thread stopper ([&signal, &abandon] ()
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
        abandon = true;
        signal.wakeAll ();
    });

    start = std::chrono::steady_clock::now ();

    CHECK (signal.waitPast (2, std::chrono::milliseconds (5000), abandon) == 2);
    CHECK (millisecondsSince (start) < 2500);

    stopper.join ();
}

static void testStreamJson ()
{
    Stream withPreset;

    withPreset.name = "klove";
    withPreset.displayName = "K-LOVE";
    withPreset.description = "Positive";
    withPreset.url = "http://example/klove";
    withPreset.preset = 13;

    const nlohmann::json json = WebJson::stream (withPreset);

    CHECK (json["name"].get<std::string> () == "klove");
    CHECK (json["display_name"].get<std::string> () == "K-LOVE");
    CHECK (json["url"].get<std::string> () == "http://example/klove");
    CHECK (json["preset"].get<int> () == 13);

    Stream noPreset;

    noPreset.name = "klove-live";
    noPreset.url = "http://example/live";

    const nlohmann::json other = WebJson::stream (noPreset);

    CHECK (other["preset"].is_null ());         // 0 is not a button, so it reports null
}

static void testStreamsJson ()
{
    Stream a;
    Stream b;

    a.name = "a";
    b.name = "b";

    const std::vector<Stream> list { a, b };
    const nlohmann::json json = WebJson::streams (list);

    CHECK (json.is_array ());
    CHECK (json.size () == 2);
    CHECK (json[0]["name"].get<std::string> () == "a");
    CHECK (json[1]["name"].get<std::string> () == "b");
}

static void testDevicesJson ()
{
    SoundTouchDevice first;
    SoundTouchDevice second;

    first.deviceId = "AAA";
    first.deviceName = "Kitchen";
    first.ipAddress = "192.168.1.10";

    second.deviceId = "BBB";
    second.deviceName = "Study";
    second.ipAddress = "192.168.1.11";

    const std::vector<SoundTouchDevice> list { first, second };
    const nlohmann::json json = WebJson::devices (list, "BBB");

    CHECK (json["default_device"].get<std::string> () == "BBB");
    CHECK (json["devices"].size () == 2);
    CHECK (json["devices"][0]["is_default"].get<bool> () == false);
    CHECK (json["devices"][1]["is_default"].get<bool> () == true);
    CHECK (json["devices"][1]["device_name"].get<std::string> () == "Study");

    const nlohmann::json none = WebJson::devices (list, "");

    CHECK (none["devices"][0]["is_default"].get<bool> () == false);
    CHECK (none["devices"][1]["is_default"].get<bool> () == false);
}

static SoundTouchDevice makeSpeaker (const std::string &id, const std::string &name, const std::string &ip,
                                     const std::string &type = "")
{
    SoundTouchDevice speaker;

    speaker.deviceId = id;
    speaker.deviceName = name;
    speaker.ipAddress = ip;
    speaker.deviceType = type;

    return (speaker);
}

static void testSpeakerConfigLoad ()
{
    SpeakerConfig config;

    // Not a devices.json at all.
    CHECK (!config.loadFromText (""));
    CHECK (!config.loadFromText ("[1, 2]"));
    CHECK (!config.loadFromText ("{ nope"));
    CHECK (config.getSpeakers ().empty ());
    CHECK (config.getDefaultSpeaker () == nullptr);

    // A file with no speakers is fine.
    CHECK (config.loadFromText ("{}"));
    CHECK (config.getSpeakers ().empty ());
    CHECK (config.getDefaultId ().empty ());

    // Speakers without an ID or an address, and repeats, are left out.
    CHECK (config.loadFromText (R"({ "default_device": "BBB", "devices": [
        { "device_id": "AAA", "device_name": "Kitchen", "ip_address": "10.0.0.1", "device_type": "SoundTouch 30" },
        { "device_id": "BBB", "device_name": "Study", "ip_address": "10.0.0.2" },
        { "device_id": "", "ip_address": "10.0.0.3" },
        { "device_id": "CCC" },
        { "device_id": "AAA", "device_name": "Again", "ip_address": "10.0.0.4" },
        "junk" ] })"));
    CHECK (config.getSpeakers ().size () == 2);
    CHECK (config.getSpeakers ()[0].deviceType == "SoundTouch 30");
    CHECK (config.findById ("AAA") != nullptr && config.findById ("AAA")->deviceName == "Kitchen");
    CHECK (config.findById ("CCC") == nullptr);
    CHECK (config.getDefaultId () == "BBB");
    CHECK (config.getDefaultSpeaker () != nullptr && config.getDefaultSpeaker ()->deviceId == "BBB");

    // With none named, the first is the default, as every command takes it.
    CHECK (config.loadFromText (R"({ "devices": [ { "device_id": "AAA", "ip_address": "10.0.0.1" } ] })"));
    CHECK (config.getDefaultId ().empty ());
    CHECK (config.getDefaultSpeaker () != nullptr && config.getDefaultSpeaker ()->deviceId == "AAA");

    // A default that is not saved is none.
    CHECK (config.loadFromText (R"({ "default_device": "ZZZ", "devices": [ { "device_id": "AAA", "ip_address": "10.0.0.1" } ] })"));
    CHECK (config.getDefaultSpeaker () == nullptr);
}

static void testSpeakerConfigRemember ()
{
    SpeakerConfig config;

    CHECK (config.loadFromText (R"({ "devices": [ { "device_id": "AAA", "device_name": "Kitchen", "ip_address": "10.0.0.1",
                                                     "device_type": "SoundTouch 30", "usn": "uuid:a" } ] })"));

    // No ID or no address: not taken.
    CHECK (!config.remember (makeSpeaker ("", "x", "10.0.0.9")));
    CHECK (!config.remember (makeSpeaker ("XYZ", "x", "")));
    CHECK (config.getSpeakers ().size () == 1);

    // A saved speaker at a new address, found without its name or model, keeps them.
    CHECK (config.remember (makeSpeaker ("AAA", "", "10.0.0.7")));
    CHECK (config.getSpeakers ().size () == 1);
    CHECK (config.findById ("AAA")->ipAddress == "10.0.0.7");
    CHECK (config.findById ("AAA")->deviceName == "Kitchen");
    CHECK (config.findById ("AAA")->deviceType == "SoundTouch 30");
    CHECK (config.findById ("AAA")->usn == "uuid:a");

    // A new one is added after.
    CHECK (config.remember (makeSpeaker ("BBB", "Study", "10.0.0.2")));
    CHECK (config.getSpeakers ().size () == 2 && config.getSpeakers ()[1].deviceId == "BBB");

    // Only a saved speaker can be the default.
    CHECK (!config.setDefault ("ZZZ"));
    CHECK (config.getDefaultId ().empty ());
    CHECK (config.setDefault ("BBB"));
    CHECK (config.getDefaultSpeaker ()->deviceId == "BBB");
}

static void testSpeakerConfigFileText ()
{
    // Another setting (groups), the order of the keys and a field this program does not know survive.
    const std::string existing = R"({
  "groups": [ { "name": "Downstairs", "members": [ "AAA" ] } ],
  "devices": [ { "device_id": "AAA", "device_name": "Kitchen", "ip_address": "10.0.0.1", "volume_cap": 60 } ],
  "default_device": "AAA"
})";
    SpeakerConfig config;

    CHECK (config.loadFromText (existing));
    CHECK (config.remember (makeSpeaker ("BBB", "Study", "10.0.0.2", "SoundTouch 10")));
    CHECK (config.setDefault ("BBB"));

    const std::string text = config.fileText (existing);
    const nlohmann::ordered_json written = nlohmann::ordered_json::parse (text);

    CHECK (text.back () == '\n');
    CHECK (written.is_object ());
    CHECK (written.begin ().key () == "groups");
    CHECK (written["groups"][0]["name"] == "Downstairs");
    CHECK (std::next (written.begin ()).key () == "devices");
    CHECK (written["default_device"] == "BBB");
    CHECK (written["devices"].size () == 2);
    CHECK (written["devices"][0]["volume_cap"] == 60);
    CHECK (written["devices"][0]["device_name"] == "Kitchen");
    CHECK (!written["devices"][0].contains ("device_type"));
    CHECK (written["devices"][1]["device_type"] == "SoundTouch 10");
    CHECK (!written["devices"][1].contains ("volume_cap"));

    // What it writes reads back the same.
    SpeakerConfig again;

    CHECK (again.loadFromText (text));
    CHECK (again.getSpeakers ().size () == 2);
    CHECK (again.getDefaultSpeaker ()->deviceName == "Study");
    CHECK (again.fileText (text) == text);

    // No file, or one that is not JSON, starts afresh.
    const nlohmann::json fresh = nlohmann::json::parse (config.fileText (""));

    CHECK (fresh["devices"].size () == 2 && fresh["default_device"] == "BBB");
    CHECK (nlohmann::json::parse (config.fileText ("{ broken"))["devices"].size () == 2);
}

static void testSpeakersJson ()
{
    SpeakerConfig saved;

    CHECK (saved.loadFromText (R"({ "default_device": "BBB", "devices": [
        { "device_id": "AAA", "device_name": "Kitchen", "ip_address": "10.0.0.1" },
        { "device_id": "BBB", "device_name": "Study", "ip_address": "10.0.0.2", "device_type": "SoundTouch 30" } ] })"));

    WebJson::SpeakerSearch search;

    // Before any search: the saved speakers, default first, and online not known.
    nlohmann::json json = WebJson::speakers (saved, {}, search, 10000, "");

    CHECK (json["has_default"] == true);
    CHECK (json["default_id"] == "BBB");
    CHECK (json["embedded"] == false);
    CHECK (json["groups"].is_array () && json["groups"].empty ());
    CHECK (json["speakers"].size () == 2);
    CHECK (json["speakers"][0]["id"] == "BBB");
    CHECK (json["speakers"][0]["default"] == true);
    CHECK (json["speakers"][0]["saved"] == true);
    CHECK (json["speakers"][0]["type"] == "SoundTouch 30");
    CHECK (json["speakers"][0]["online"].is_null ());
    CHECK (json["speakers"][0]["last_seen_ms"].is_null ());
    CHECK (json["speakers"][1]["id"] == "AAA");
    CHECK (json["speakers"][1]["default"] == false);
    CHECK (json["discovery"]["active"] == false);
    CHECK (json["discovery"]["passes"] == 0);
    CHECK (json["discovery"]["last_ms"].is_null ());

    // After searching: Study answered lately at a new address; Kitchen did not; two new ones by name.
    search.active = true;
    search.searching = true;
    search.passes = 3;
    search.lastAgoMs = 500;

    std::vector<WebJson::SeenSpeaker> seen;

    seen.push_back ({ makeSpeaker ("ZZZ", "Patio", "10.0.0.9"), 1000 });
    seen.push_back ({ makeSpeaker ("BBB", "", "10.0.0.22"), 2000 });
    seen.push_back ({ makeSpeaker ("YYY", "Bedroom", "10.0.0.8", "SoundTouch 10"), 30000 });

    json = WebJson::speakers (saved, seen, search, 10000, "10.0.0.2");

    CHECK (json["embedded"] == true);
    CHECK (json["active_ip"] == "10.0.0.2");
    CHECK (json["speakers"].size () == 4);

    const nlohmann::json &study = json["speakers"][0];

    CHECK (study["id"] == "BBB");
    CHECK (study["name"] == "Study");
    CHECK (study["ip"] == "10.0.0.22");
    CHECK (study["saved_ip"] == "10.0.0.2");
    CHECK (study["online"] == true);
    CHECK (study["last_seen_ms"] == 2000);
    CHECK (study["active"] == true);

    const nlohmann::json &kitchen = json["speakers"][1];

    CHECK (kitchen["id"] == "AAA");
    CHECK (kitchen["online"] == false);
    CHECK (kitchen["active"] == false);
    CHECK (!kitchen.contains ("saved_ip"));

    CHECK (json["speakers"][2]["name"] == "Bedroom");
    CHECK (json["speakers"][2]["saved"] == false);
    CHECK (json["speakers"][2]["online"] == false);
    CHECK (json["speakers"][2]["type"] == "SoundTouch 10");
    CHECK (json["speakers"][3]["name"] == "Patio");
    CHECK (json["speakers"][3]["online"] == true);
    CHECK (json["discovery"]["active"] == true);
    CHECK (json["discovery"]["searching"] == true);
    CHECK (json["discovery"]["passes"] == 3);
    CHECK (json["discovery"]["last_ms"] == 500);

    // No speakers saved: no default, only what was found.
    SpeakerConfig none;

    CHECK (none.loadFromText ("{}"));
    json = WebJson::speakers (none, seen, search, 10000, "");

    CHECK (json["has_default"] == false);
    CHECK (json["default_id"] == "");
    CHECK (json["speakers"].size () == 3);
    CHECK (json["speakers"][0]["name"] == "Bedroom");
    CHECK (json["speakers"][1]["name"] == "Patio");
    CHECK (json["speakers"][2]["id"] == "BBB");
}

static void testParseSpeakerId ()
{
    std::string id = "keep";

    CHECK (WebJson::parseSpeakerId (R"({"id":"A81B6A536A98"})", id) && id == "A81B6A536A98");

    id = "keep";
    CHECK (!WebJson::parseSpeakerId ("", id));
    CHECK (!WebJson::parseSpeakerId ("not json", id));
    CHECK (!WebJson::parseSpeakerId ("[]", id));
    CHECK (!WebJson::parseSpeakerId ("{}", id));
    CHECK (!WebJson::parseSpeakerId (R"({"id":""})", id));
    CHECK (!WebJson::parseSpeakerId (R"({"id":42})", id));
    CHECK (!WebJson::parseSpeakerId (R"({"id":")" + std::string (65, 'A') + R"("})", id));
    CHECK (id == "keep");
    CHECK (WebJson::parseSpeakerId (R"({"id":")" + std::string (64, 'A') + R"("})", id) && id.size () == 64);
}

static void testNowPlayingJson ()
{
    SoundTouchClient::NowPlaying now;

    now.source = "UPNP";
    now.status = "PLAY_STATE";
    now.location = "http://host:8899/stream/klove";

    const nlohmann::json json = WebJson::nowPlaying (now, "K-LOVE");

    CHECK (json["source"].get<std::string> () == "UPNP");
    CHECK (json["status"].get<std::string> () == "PLAY_STATE");
    CHECK (json["station"].get<std::string> () == "K-LOVE");
    CHECK (json["location"].get<std::string> () == "http://host:8899/stream/klove");

    // With no volume given, the fields are present but null, never a made-up 0.
    CHECK (json["volume"].is_null ());
    CHECK (json["muted"].is_null ());

    // Likewise a station with no preset given: no preset button stands for it.
    CHECK (json["station_preset"].is_null ());

    const nlohmann::json onPreset = WebJson::nowPlaying (now, "K-LOVE", SoundTouchClient::Volume (), 1);

    CHECK (onPreset["station_preset"].get<int> () == 1);

    // The stream's own name, by which the dashboard finds its row; "" when none is given.
    CHECK (json["station_name"].get<std::string> () == "");

    const nlohmann::json named = WebJson::nowPlaying (now, "K-LOVE", SoundTouchClient::Volume (), 1, "klove");

    CHECK (named["station_name"].get<std::string> () == "klove");
}

static void testNowPlayingVolumeJson ()
{
    SoundTouchClient::NowPlaying now;

    now.source = "UPNP";
    now.status = "PLAY_STATE";

    SoundTouchClient::Volume vol;

    vol.target = 15;
    vol.actual = 15;
    vol.muted = false;
    vol.valid = true;

    const nlohmann::json json = WebJson::nowPlaying (now, "K-LOVE", vol);

    CHECK (json["volume"].get<int> () == 15);
    CHECK (json["muted"].get<bool> () == false);

    SoundTouchClient::Volume muted;

    muted.actual = 0;
    muted.muted = true;
    muted.valid = true;

    const nlohmann::json mjson = WebJson::nowPlaying (now, "K-LOVE", muted);

    CHECK (mjson["volume"].get<int> () == 0);
    CHECK (mjson["muted"].get<bool> () == true);

    // An invalid read reports null, the same as passing no volume at all.
    SoundTouchClient::Volume unknown;

    const nlohmann::json ujson = WebJson::nowPlaying (now, "K-LOVE", unknown);

    CHECK (ujson["volume"].is_null ());
    CHECK (ujson["muted"].is_null ());
}

static void testEntityTag ()
{
    const std::string tag = HttpUtil::entityTag ("{\"streams\": []}");

    CHECK (tag.size () > 2 && tag.front () == '"' && tag.back () == '"');
    CHECK (tag == HttpUtil::entityTag ("{\"streams\": []}"));
    CHECK (tag != HttpUtil::entityTag ("{\"streams\": [] }"));
    CHECK (HttpUtil::entityTag ("") != tag);
    CHECK (HttpUtil::entityTag ("") == HttpUtil::entityTag (""));
}

static void testIfMatches ()
{
    const std::string tag = HttpUtil::entityTag ("abc");

    CHECK (HttpUtil::ifMatches (tag, tag));
    CHECK (HttpUtil::ifMatches ("  " + tag + " ", tag));
    CHECK (HttpUtil::ifMatches ("*", tag));
    CHECK (HttpUtil::ifMatches ("\"other\", " + tag, tag));
    CHECK (!HttpUtil::ifMatches ("\"other\"", tag));
    CHECK (!HttpUtil::ifMatches ("", tag));
    CHECK (!HttpUtil::ifMatches ("W/" + tag, tag));          // a weak tag never matches If-Match
    CHECK (!HttpUtil::ifMatches (tag.substr (1, tag.size () - 2), tag));   // unquoted
}

static void testParsePlayStream ()
{
    std::string name = "unchanged";

    CHECK (WebJson::parsePlayStream ("{\"stream\": \"klove-70s\"}", name));
    CHECK (name == "klove-70s");

    name = "unchanged";

    CHECK (!WebJson::parsePlayStream ("{\"stream\": \"two words\"}", name));
    CHECK (!WebJson::parsePlayStream ("{\"stream\": \"\"}", name));
    CHECK (!WebJson::parsePlayStream ("{\"stream\": 1}", name));
    CHECK (!WebJson::parsePlayStream ("{\"name\": \"klove\"}", name));
    CHECK (!WebJson::parsePlayStream ("[\"klove\"]", name));
    CHECK (!WebJson::parsePlayStream ("klove", name));
    CHECK (name == "unchanged");
}

static void testStreamRules ()
{
    CHECK (StreamConfig::isValidName ("klove-70s"));
    CHECK (StreamConfig::isValidName ("A.b_c-9"));
    CHECK (!StreamConfig::isValidName (""));
    CHECK (!StreamConfig::isValidName ("two words"));
    CHECK (!StreamConfig::isValidName ("slash/ed"));
    CHECK (!StreamConfig::isValidName ("caf\xc3\xa9"));
    CHECK (StreamConfig::isValidName (std::string (64, 'a')));
    CHECK (!StreamConfig::isValidName (std::string (65, 'a')));

    CHECK (StreamConfig::isValidUrl ("http://maestro.emfcdn.com/stream_for/k-love/iheart/aac"));
    CHECK (StreamConfig::isValidUrl ("HTTPS://example.com"));
    CHECK (StreamConfig::isValidUrl ("http://user:pass@host:8000/live?x=1"));
    CHECK (StreamConfig::isValidUrl ("http://192.168.1.5:8000"));
    CHECK (!StreamConfig::isValidUrl ("ftp://example.com/x"));
    CHECK (!StreamConfig::isValidUrl ("example.com/stream"));
    CHECK (!StreamConfig::isValidUrl ("http://"));
    CHECK (!StreamConfig::isValidUrl ("http:///path"));
    CHECK (!StreamConfig::isValidUrl ("http://:8000/x"));
    CHECK (!StreamConfig::isValidUrl ("http://user@/x"));
    CHECK (!StreamConfig::isValidUrl ("http://host/a b"));
    CHECK (!StreamConfig::isValidUrl ("http://host/a\tb"));
    CHECK (!StreamConfig::isValidUrl ("javascript:alert(1)"));
    CHECK (!StreamConfig::isValidUrl ("http://h/" + std::string (StreamConfig::MAX_URL, 'x')));
}

// Whether problems names this field of the stream at index.
static bool hasProblem (const nlohmann::json &problems, size_t index, const std::string &field)
{
    for (const nlohmann::json &problem : problems)
    {
        if (problem.value ("index", static_cast<size_t> (-1)) == index && problem.value ("field", std::string ()) == field)
        {
            return (true);
        }
    }

    return (false);
}

static void testParseStreams ()
{
    std::vector<Stream> streams;
    nlohmann::json problems;

    // Trimmed; a missing display name is the name; no preset, null and 0 all mean none.
    CHECK (WebJson::parseStreams (
        "{\"streams\": ["
        "{\"name\": \" klove \", \"display_name\": \"K-LOVE\", \"description\": \" Positive \", \"url\": \" http://a/k \", \"preset\": 1},"
        "{\"name\": \"live\", \"url\": \"https://a/live\"},"
        "{\"name\": \"null\", \"url\": \"http://a/n\", \"preset\": null, \"display_name\": null},"
        "{\"name\": \"zero\", \"url\": \"http://a/z\", \"preset\": 0},"
        "{\"name\": \"combo\", \"url\": \"http://a/c\", \"preset\": 111, \"extra\": true}]}", streams, problems));
    CHECK (problems.empty ());
    CHECK (streams.size () == 5);
    CHECK (streams[0].name == "klove" && streams[0].url == "http://a/k" && streams[0].description == "Positive");
    CHECK (streams[0].preset == 1 && streams[0].displayName == "K-LOVE");
    CHECK (streams[1].displayName == "live" && streams[1].preset == 0);
    CHECK (streams[2].preset == 0 && streams[2].displayName == "null");
    CHECK (streams[3].preset == 0);
    CHECK (streams[4].preset == 111);

    // An empty list is a list.
    CHECK (WebJson::parseStreams ("{\"streams\": []}", streams, problems));
    CHECK (streams.empty ());

    // Each problem is named by stream and field; a duplicate on both streams that share it.
    streams.assign (1, Stream ());

    CHECK (!WebJson::parseStreams (
        "{\"streams\": ["
        "{\"name\": \"a b\", \"url\": \"ftp://x\", \"preset\": 7},"
        "{\"name\": \"dup\", \"url\": \"http://x\", \"preset\": 2},"
        "{\"name\": \"dup\", \"url\": \"http://y\", \"preset\": 2},"
        "{\"url\": \"http://z\", \"preset\": \"1\"},"
        "{\"name\": \"n\", \"preset\": 1.5},"
        "{\"name\": \"m\", \"url\": \"http://m\", \"preset\": 1000, \"display_name\": 5},"
        "{\"name\": \"c\", \"url\": \"http://c\", \"description\": \"bad\\u0007bell\", \"preset\": -1},"
        "\"not a stream\"]}", streams, problems));
    CHECK (streams.size () == 1);                     // left alone
    CHECK (hasProblem (problems, 0, "name"));
    CHECK (hasProblem (problems, 0, "url"));
    CHECK (hasProblem (problems, 0, "preset"));
    CHECK (hasProblem (problems, 1, "name") && hasProblem (problems, 2, "name"));
    CHECK (hasProblem (problems, 1, "preset") && hasProblem (problems, 2, "preset"));
    CHECK (hasProblem (problems, 3, "name"));         // missing
    CHECK (hasProblem (problems, 3, "preset"));       // a string
    CHECK (hasProblem (problems, 4, "url"));          // missing
    CHECK (hasProblem (problems, 4, "preset"));       // not whole
    CHECK (hasProblem (problems, 5, "preset"));       // too large
    CHECK (hasProblem (problems, 5, "display_name")); // not text
    CHECK (hasProblem (problems, 6, "description"));  // a control character
    CHECK (hasProblem (problems, 6, "preset"));       // negative
    CHECK (hasProblem (problems, 7, ""));             // not an object
    CHECK (!hasProblem (problems, 5, "name"));

    CHECK (!WebJson::parseStreams ("{\"streams\": [{\"name\": \"x\", \"url\": \"http://x\", \"display_name\": \""
                                   + std::string (StreamConfig::MAX_DISPLAY_NAME + 1, 'd') + "\"}]}", streams, problems));
    CHECK (hasProblem (problems, 0, "display_name"));

    // Not a stream list at all: one problem for the whole request.
    for (const char *body : { "[]", "{}", "{\"streams\": {}}", "not json" })
    {
        CHECK (!WebJson::parseStreams (body, streams, problems));
        CHECK (problems.size () == 1 && problems[0].contains ("error") && !problems[0].contains ("index"));
    }
}

static Stream makeStream (const char *name, const char *display, const char *url, int preset, const char *description = "")
{
    Stream stream;

    stream.name = name;
    stream.displayName = display;
    stream.url = url;
    stream.preset = preset;
    stream.description = description;

    return (stream);
}

static void testFileText ()
{
    // Settings, unknown keys and their order are kept; a stream keeps fields it alone knows by its name.
    const std::string existing =
        "{\n  \"note\": \"mine\",\n  \"combo_window_ms\": 900,\n  \"streams\": [\n"
        "    {\"name\": \"klove\", \"display_name\": \"K-LOVE\", \"url\": \"http://a/k\", \"preset\": 1, \"genre\": \"pop\"},\n"
        "    {\"name\": \"gone\", \"display_name\": \"Gone\", \"url\": \"http://a/g\", \"extra\": 1}\n  ],\n  \"after\": [1, 2]\n}\n";

    const std::vector<Stream> streams { makeStream ("new", "New", "http://a/n", 0, "Fresh"),
                                        makeStream ("klove", "K-LOVE Radio", "http://a/k2", 2) };
    const std::string text = StreamConfig::fileText (streams, existing);
    const nlohmann::ordered_json document = nlohmann::ordered_json::parse (text);

    std::vector<std::string> keys;

    for (auto it = document.begin (); it != document.end (); ++it)
    {
        keys.push_back (it.key ());
    }

    CHECK ((keys == std::vector<std::string> { "note", "combo_window_ms", "streams", "after" }));
    CHECK (document["combo_window_ms"] == 900 && document["note"] == "mine" && document["after"].size () == 2);
    CHECK (document["streams"].size () == 2);

    const nlohmann::ordered_json &added = document["streams"][0];
    const nlohmann::ordered_json &kept = document["streams"][1];

    CHECK (added["name"] == "new" && added["description"] == "Fresh" && !added.contains ("preset"));
    CHECK (!added.contains ("extra") && !added.contains ("genre"));
    CHECK (kept["display_name"] == "K-LOVE Radio" && kept["url"] == "http://a/k2" && kept["preset"] == 2);
    CHECK (kept["genre"] == "pop");
    CHECK (!kept.contains ("description"));          // empty, so left out

    std::vector<std::string> fields;

    for (auto it = kept.begin (); it != kept.end (); ++it)
    {
        fields.push_back (it.key ());
    }

    CHECK ((fields == std::vector<std::string> { "name", "display_name", "url", "preset", "genre" }));
    CHECK (text.back () == '\n');

    // What is written loads back as written.
    StreamConfig loaded;

    CHECK (loaded.loadFromText (text, true));
    CHECK (loaded.getStreams ().size () == 2);
    CHECK (loaded.getComboWindowMs () == 900);
    CHECK (loaded.findByPreset (2) != nullptr && loaded.findByPreset (2)->name == "klove");
    CHECK (loaded.findByName ("new") != nullptr && loaded.findByName ("new")->preset == 0);

    // A new file, or one that is not JSON, starts afresh with the default combo window.
    for (const char *before : { "", "not json", "[1, 2]" })
    {
        const nlohmann::ordered_json fresh = nlohmann::ordered_json::parse (StreamConfig::fileText (streams, before));

        CHECK (fresh["combo_window_ms"] == 700 && fresh["streams"].size () == 2);
        CHECK (fresh.begin ().key () == "combo_window_ms");
    }

    // The same list over its own file gives the very same text: a save with no change writes nothing.
    CHECK (StreamConfig::fileText (streams, text) == text);
}

static std::string fileContents (const std::string &path)
{
    std::ifstream in (path, std::ios::binary);

    return (std::string (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char> ()));
}

static void testSaveFile ()
{
    char pattern[] = "/tmp/cxstcc-webtest-XXXXXX";
    const char *directory = mkdtemp (pattern);

    CHECK (directory != nullptr);

    if (directory == nullptr)
    {
        return;
    }

    const std::string path = std::string (directory) + "/streams.json";
    std::string error;

    // A first save makes the file, and has nothing to keep.
    CHECK (StreamConfig::saveFile (path, "one\n", "", error));
    CHECK (fileContents (path) == "one\n");
    CHECK (access ((path + ".bak").c_str (), F_OK) != 0);

    // The file's own permissions carry over to its replacement.
    chmod (path.c_str (), 0600);

    // A second keeps what it replaces.
    CHECK (StreamConfig::saveFile (path, "two\n", "one\n", error));
    CHECK (fileContents (path) == "two\n");
    CHECK (fileContents (path + ".bak") == "one\n");
    CHECK (access ((path + ".tmp").c_str (), F_OK) != 0);

    struct stat info;

    CHECK (stat (path.c_str (), &info) == 0 && (info.st_mode & 0777) == 0600);

    // Where it cannot be written, it says why.
    error.clear ();

    CHECK (!StreamConfig::saveFile (std::string (directory) + "/missing/streams.json", "x", "", error));
    CHECK (error.find ("cannot write") != std::string::npos && error.find ("No such file") != std::string::npos);

    unlink (path.c_str ());
    unlink ((path + ".bak").c_str ());
    rmdir (directory);
}

int main ()
{
    testFirstLine ();
    testParseRequestLine ();
    testPercentDecode ();
    testQueryValue ();
    testParseUnsigned ();
    testHeaderValue ();
    testMediaType ();
    testParseVolume ();
    testParsePlayback ();
    testParsePower ();
    testParsePreset ();
    testParseSkipAndSource ();
    testSelectAndButtons ();
    testLiveSignal ();
    testStreamJson ();
    testStreamsJson ();
    testDevicesJson ();
    testSpeakerConfigLoad ();
    testSpeakerConfigRemember ();
    testSpeakerConfigFileText ();
    testSpeakersJson ();
    testParseSpeakerId ();
    testNowPlayingJson ();
    testNowPlayingVolumeJson ();
    testEntityTag ();
    testIfMatches ();
    testParsePlayStream ();
    testStreamRules ();
    testParseStreams ();
    testFileText ();
    testSaveFile ();

    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";

    if (g_failures != 0)
    {
        std::cerr << g_failures << " check(s) failed\n";
        return (1);
    }

    std::cout << "All web unit tests passed.\n";

    return (0);
}
