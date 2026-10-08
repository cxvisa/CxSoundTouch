// A small, dependency-free unit test for the web server's pure logic: HTTP request-line parsing, JSON
// serialisation and the live-change signal. It links only HttpUtil and WebJson (no sockets, curl or
// pugixml; LiveSignal is header-only), so it builds and runs anywhere the main program does. Run with:
// make test
#include "HttpUtil.h"
#include "WebJson.h"
#include "LiveSignal.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
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
    testLiveSignal ();
    testStreamJson ();
    testStreamsJson ();
    testDevicesJson ();
    testNowPlayingJson ();
    testNowPlayingVolumeJson ();

    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";

    if (g_failures != 0)
    {
        std::cerr << g_failures << " check(s) failed\n";
        return (1);
    }

    std::cout << "All web unit tests passed.\n";

    return (0);
}
