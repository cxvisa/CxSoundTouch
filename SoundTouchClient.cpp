#include "SoundTouchClient.h"
#include "Say.h"
#include <curl/curl.h>
#include <pugixml.hpp>
#include <iostream>
#include <sstream>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>

std::string playingStreamUrl (const std::string &nowPlayingXml)
{
    pugi::xml_document doc;

    if (!doc.load_string (nowPlayingXml.c_str ()))
    {
        return ("");
    }

    const pugi::xml_node nowPlaying = doc.select_node ("//nowPlaying").node ();

    if (!nowPlaying || std::strcmp (nowPlaying.attribute ("source").value (), "UPNP") != 0)
    {
        return ("");
    }

    const char *playStatus = nowPlaying.child ("playStatus").child_value ();

    if (std::strcmp (playStatus, "PLAY_STATE") != 0 && std::strcmp (playStatus, "BUFFERING_STATE") != 0)
    {
        return ("");
    }

    const std::string location = nowPlaying.child ("ContentItem").attribute ("location").value ();

    // The speaker reports this placeholder when it was handed a stream without DIDL-Lite metadata.
    if (location == "unplayable location")
    {
        return ("");
    }

    return (location);
}

SoundTouchClient::SoundTouchClient (const std::string &deviceIp)
    : m_deviceIp (deviceIp),
      m_restUrl ("http://" + deviceIp + ":8090"),
      m_controlUrl ("http://" + deviceIp + ":8091/AVTransport/Control")
{
}

SoundTouchClient::~SoundTouchClient ()
{
}

static std::string xmlEscape (const std::string &input)
{
    std::string output;

    output.reserve (input.size ());

    for (const char c : input)
    {
        switch (c)
        {
            case '&'  : output += "&amp;";  break;
            case '<'  : output += "&lt;";   break;
            case '>'  : output += "&gt;";   break;
            case '"'  : output += "&quot;"; break;
            case '\'' : output += "&apos;"; break;
            default   : output += c;        break;
        }
    }

    return (output);
}

std::string SoundTouchClient::buildSetAVTransportURISoap (const std::string &streamUrl, const std::string &title,
                                                          const std::string &artist, const std::string &album) const
{
    const std::string url = xmlEscape (streamUrl);

    // The speaker reads the display name from DIDL-Lite; <res> is required or it faults with 402.
    // It maps dc:title to both itemName and track, and dc:creator to artist, ignoring upnp:artist.
    std::ostringstream didl;

    didl << "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\""
         << " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
         << " xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">"
         << "<item id=\"1\" parentID=\"0\" restricted=\"1\">"
         << "<dc:title>" << xmlEscape (title) << "</dc:title>"
         << "<dc:creator>" << xmlEscape (artist) << "</dc:creator>"
         << "<upnp:artist>" << xmlEscape (artist) << "</upnp:artist>"
         << "<upnp:album>" << xmlEscape (album) << "</upnp:album>"
         << "<upnp:class>object.item.audioItem.audioBroadcast</upnp:class>"
         << "<res protocolInfo=\"http-get:*:audio/aac:*\">" << url << "</res>"
         << "</item>"
         << "</DIDL-Lite>";

    std::ostringstream oss;

    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
        << "<s:Body>\n"
        << "<u:SetAVTransportURI xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\n"
        << "<InstanceID>0</InstanceID>\n"
        << "<CurrentURI>" << url << "</CurrentURI>\n"
        << "<CurrentURIMetaData>" << xmlEscape (didl.str ()) << "</CurrentURIMetaData>\n"
        << "</u:SetAVTransportURI>\n"
        << "</s:Body>\n"
        << "</s:Envelope>";

    return (oss.str ());
}

std::string SoundTouchClient::buildPlaySoap () const
{
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
        "<s:Body>\n"
        "<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\n"
        "<InstanceID>0</InstanceID>\n"
        "<Speed>1</Speed>\n"
        "</u:Play>\n"
        "</s:Body>\n"
        "</s:Envelope>"
    );
}

std::string SoundTouchClient::buildStopSoap () const
{
    return (
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
        "<s:Body>\n"
        "<u:Stop xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\n"
        "<InstanceID>0</InstanceID>\n"
        "</u:Stop>\n"
        "</s:Body>\n"
        "</s:Envelope>"
    );
}

std::string SoundTouchClient::buildStorePresetXml (int presetId, const std::string &streamUrl, const std::string &stationName) const
{
    std::ostringstream oss;

    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<preset id=\"" << presetId << "\">\n"
        << "<ContentItem\n"
        << "source=\"UPNP\"\n"
        << "location=\"" << streamUrl << "\"\n"
        << "sourceAccount=\"UpnPUserName\"\n"
        << "isPresetable=\"true\">\n"
        << "<itemName>" << stationName << "</itemName>\n"
        << "</ContentItem>\n"
        << "</preset>";

    return (oss.str ());
}

std::string SoundTouchClient::buildKeyPressXml (int presetId, const std::string &state) const
{
    std::ostringstream oss;

    // The body is a KEY_VALUE enum name, not a nested element. A nested <preset> is accepted with
    // a success status but often does nothing, which makes it look like flaky hardware.
    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<key state=\"" << state << "\" sender=\"Gabbo\">PRESET_" << presetId << "</key>";

    return (oss.str ());
}

static size_t curlWriteCallback (void *contents, size_t size, size_t nmemb, void *userp)
{
    const size_t totalSize = size * nmemb;
    std::string *response = static_cast<std::string *> (userp);

    response->append (static_cast<char *> (contents), totalSize);

    return (totalSize);
}

bool SoundTouchClient::upnpRequest (
    const std::string &soapAction,
    const std::string &body,
    std::string &response
)
{
    CURL *curl = curl_easy_init ();

    if (curl == nullptr)
    {
        Say (std::cerr) << "Failed to initialize curl\n";
        return (false);
    }

    struct curl_slist *headers = nullptr;

    headers = curl_slist_append (headers, "Content-Type: text/xml; charset=\"utf-8\"");

    const std::string soapActionHeader = "SOAPACTION: \"" + soapAction + "\"";

    headers = curl_slist_append (headers, soapActionHeader.c_str ());

    curl_easy_setopt (curl, CURLOPT_URL, m_controlUrl.c_str ());
    curl_easy_setopt (curl, CURLOPT_POST, 1L);
    curl_easy_setopt (curl, CURLOPT_POSTFIELDS, body.c_str ());
    curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, 10L);

    const CURLcode res = curl_easy_perform (curl);

    curl_slist_free_all (headers);
    curl_easy_cleanup (curl);

    if (res != CURLE_OK)
    {
        Say (std::cerr) << "UPnP request failed: " << curl_easy_strerror (res) << "\n";
        return (false);
    }

    return (true);
}

bool SoundTouchClient::restGet (const std::string &endpoint, std::string &response, long timeoutMs, bool quiet)
{
    CURL *curl = curl_easy_init ();

    if (curl == nullptr)
    {
        Say (std::cerr) << "Failed to initialize curl\n";
        return (false);
    }

    const std::string url = m_restUrl + endpoint;

    curl_easy_setopt (curl, CURLOPT_URL, url.c_str ());
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt (curl, CURLOPT_TIMEOUT_MS, timeoutMs);

    const CURLcode res = curl_easy_perform (curl);

    curl_easy_cleanup (curl);

    if (res != CURLE_OK)
    {
        if (!quiet)
        {
            Say (std::cerr) << "REST GET failed: " << curl_easy_strerror (res) << "\n";
        }

        return (false);
    }

    return (true);
}

bool SoundTouchClient::restPost (
    const std::string &endpoint,
    const std::string &body,
    std::string &response,
    long timeoutMs
)
{
    CURL *curl = curl_easy_init ();

    if (curl == nullptr)
    {
        Say (std::cerr) << "Failed to initialize curl\n";
        return (false);
    }

    const std::string url = m_restUrl + endpoint;

    struct curl_slist *headers = nullptr;

    headers = curl_slist_append (headers, "Content-Type: application/xml");

    curl_easy_setopt (curl, CURLOPT_URL, url.c_str ());
    curl_easy_setopt (curl, CURLOPT_POST, 1L);
    curl_easy_setopt (curl, CURLOPT_POSTFIELDS, body.c_str ());
    curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt (curl, CURLOPT_TIMEOUT_MS, timeoutMs);

    const CURLcode res = curl_easy_perform (curl);

    curl_slist_free_all (headers);
    curl_easy_cleanup (curl);

    if (res != CURLE_OK)
    {
        Say (std::cerr) << "REST POST failed: " << curl_easy_strerror (res) << "\n";
        return (false);
    }

    return (true);
}

bool SoundTouchClient::sendStream (const std::string &streamUrl, const std::string &title,
                                   const std::string &artist, const std::string &album)
{
    std::string response;

    const std::string setUriAction = "urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI";
    const std::string setUriBody = buildSetAVTransportURISoap (streamUrl, title, artist, album);

    if (!upnpRequest (setUriAction, setUriBody, response))
    {
        Say (std::cerr) << "SetAVTransportURI failed\n";
        return (false);
    }

    if (response.find ("<s:Fault>") != std::string::npos || response.find ("UPnPError") != std::string::npos)
    {
        Say (std::cerr) << "\nERROR: Device rejected SetAVTransportURI request\n";
        Say (std::cerr) << "Response: " << response << "\n";
        return (false);
    }

    response.clear ();

    const std::string playAction = "urn:schemas-upnp-org:service:AVTransport:1#Play";

    if (!upnpRequest (playAction, buildPlaySoap (), response))
    {
        Say (std::cerr) << "Play failed\n";
        return (false);
    }

    if (response.find ("<s:Fault>") != std::string::npos || response.find ("UPnPError") != std::string::npos)
    {
        Say (std::cerr) << "\nERROR: Device rejected Play request\n";
        Say (std::cerr) << "Response: " << response << "\n";
        return (false);
    }

    return (true);
}

bool SoundTouchClient::waitUntilPlaying (const std::string &streamUrl, const Superseded &superseded)
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now () + std::chrono::milliseconds (START_TIMEOUT_MS);

    while (std::chrono::steady_clock::now () < deadline)
    {
        if (superseded && superseded ())
        {
            return (false);
        }

        std::string response;

        if (restGet ("/nowPlaying", response) && playingStreamUrl (response) == streamUrl)
        {
            return (true);
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (200));
    }

    return (false);
}

bool SoundTouchClient::stopQuietly ()
{
    std::string response;

    return (upnpRequest ("urn:schemas-upnp-org:service:AVTransport:1#Stop", buildStopSoap (), response));
}

bool SoundTouchClient::playStream (const std::string &streamUrl, const std::string &stationName,
                                   const std::string &title, const std::string &artist,
                                   const Superseded &superseded, const BeforeRetry &beforeRetry)
{
    Say () << "\nSetting " << stationName << " stream...\n";
    Say () << "URL: " << streamUrl << "\n";

    if (!title.empty ())
    {
        Say () << "Now playing: " << (artist.empty () ? title : artist + " - " + title) << "\n";
    }

    Say () << "\n";

    const std::string showTitle  = title.empty ()  ? stationName : title;
    const std::string showArtist = artist.empty () ? stationName : artist;

    if (!sendStream (streamUrl, showTitle, showArtist, stationName))
    {
        return (false);
    }

    if (!waitUntilPlaying (streamUrl, superseded))
    {
        // A newer press stopped the speaker on purpose; retrying would undo it.
        if (superseded && superseded ())
        {
            return (false);
        }

        // Seen after a burst of button presses: the player sat paused and never asked for the
        // stream. Stopping and starting again gives a passing hiccup a second chance.
        Say () << "The speaker took the stream but has not started it; trying once more...\n";

        // First, so that what the Stop sets off is seen as this play's own doing.
        if (beforeRetry)
        {
            beforeRetry ();
        }

        stopQuietly ();

        if (!sendStream (streamUrl, showTitle, showArtist, stationName) || !waitUntilPlaying (streamUrl, superseded))
        {
            if (superseded && superseded ())
            {
                return (false);
            }

            Say (std::cerr) << "\nThe speaker accepts commands but will not play; its player looks stuck.\n";
            Say (std::cerr) << "Standby from the remote did not clear it last time; a reboot did:\n";
            Say (std::cerr) << "  printf 'sys reboot\\r\\n' | nc " << m_deviceIp << " 17000\n\n";
            return (false);
        }
    }

    Say () << stationName << " is playing.\n\n";

    return (true);
}

bool SoundTouchClient::updateTrackInfo (const std::string &streamUrl, const std::string &stationName,
                                        const std::string &title, const std::string &artist)
{
    // Quiet on purpose: the caller is already printing the song it just detected.
    return (sendStream (streamUrl, title, artist.empty () ? stationName : artist, stationName));
}

std::string SoundTouchClient::currentStreamUrl ()
{
    std::string response;

    if (!restGet ("/nowPlaying", response))
    {
        return ("");
    }

    return (playingStreamUrl (response));
}

SoundTouchClient::NowPlaying SoundTouchClient::nowPlaying ()
{
    return (glance (10000));
}

SoundTouchClient::NowPlaying SoundTouchClient::glance (long timeoutMs)
{
    NowPlaying now;
    std::string response;
    pugi::xml_document doc;

    if (!restGet ("/nowPlaying", response, timeoutMs, timeoutMs < 10000) || !doc.load_string (response.c_str ()))
    {
        return (now);
    }

    const pugi::xml_node nowPlaying = doc.child ("nowPlaying");

    now.source = nowPlaying.attribute ("source").value ();
    now.status = nowPlaying.child_value ("playStatus");
    now.location = nowPlaying.child ("ContentItem").attribute ("location").value ();

    return (now);
}

SoundTouchClient::Volume SoundTouchClient::volume (long timeoutMs)
{
    Volume vol;
    std::string response;
    pugi::xml_document doc;

    if (!restGet ("/volume", response, timeoutMs, timeoutMs < 10000) || !doc.load_string (response.c_str ()))
    {
        return (vol);
    }

    const pugi::xml_node node = doc.child ("volume");

    if (!node)
    {
        return (vol);
    }

    vol.target = node.child ("targetvolume").text ().as_int ();
    vol.actual = node.child ("actualvolume").text ().as_int ();
    vol.muted = (std::strcmp (node.child_value ("muteenabled"), "true") == 0);
    vol.valid = true;

    return (vol);
}

bool SoundTouchClient::setVolume (int level, long timeoutMs)
{
    std::string response;

    // The speaker answers <status>/volume</status>, or <errors> when it refuses.
    return (restPost ("/volume", "<volume>" + std::to_string (level) + "</volume>", response, timeoutMs)
            && response.find ("<errors") == std::string::npos);
}

bool SoundTouchClient::pressKey (const std::string &key, long timeoutMs)
{
    for (const char *state : { "press", "release" })
    {
        std::string response;
        const std::string body = std::string ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<key state=\"") + state
                                 + "\" sender=\"Gabbo\">" + key + "</key>";

        if (!restPost ("/key", body, response, timeoutMs) || response.find ("<errors") != std::string::npos)
        {
            return (false);
        }
    }

    return (true);
}

bool SoundTouchClient::selectSource (const std::string &source, const std::string &account, long timeoutMs)
{
    std::string response;
    std::string body = "<ContentItem source=\"" + xmlEscape (source) + "\"";

    if (!account.empty ())
    {
        body += " sourceAccount=\"" + xmlEscape (account) + "\"";
    }

    body += "></ContentItem>";

    return (restPost ("/select", body, response, timeoutMs) && response.find ("<errors") == std::string::npos);
}

bool SoundTouchClient::stop ()
{
    Say () << "\nStopping playback...\n";

    std::string response;

    const std::string stopAction = "urn:schemas-upnp-org:service:AVTransport:1#Stop";
    const std::string stopBody = buildStopSoap ();

    if (!upnpRequest (stopAction, stopBody, response))
    {
        Say (std::cerr) << "Stop failed\n";
        return (false);
    }

    Say () << "Stopped.\n\n";

    return (true);
}

bool SoundTouchClient::status ()
{
    std::string response;

    if (!restGet ("/nowPlaying", response))
    {
        return (false);
    }

    Say () << response << "\n";

    return (true);
}

bool SoundTouchClient::presets ()
{
    Say () << "Current presets:\n\n";

    std::string response;

    if (!restGet ("/presets", response))
    {
        return (false);
    }

    Say () << response << "\n";

    return (true);
}

bool SoundTouchClient::savePreset (int presetId, const std::string &streamUrl, const std::string &stationName)
{
    if (presetId < 1 || presetId > 6)
    {
        Say (std::cerr) << "Error: preset must be between 1 and 6\n";
        return (false);
    }

    const std::string body = buildStorePresetXml (presetId, streamUrl, stationName);
    std::string saveResponse;

    if (!restPost ("/storePreset", body, saveResponse))
    {
        return (false);
    }

    return (saveResponse.find ("<errors") == std::string::npos);
}

bool SoundTouchClient::pressButton (int button)
{
    std::string response;

    if (!restPost ("/key", buildKeyPressXml (button, "press"), response))
    {
        return (false);
    }

    response.clear ();

    return (restPost ("/key", buildKeyPressXml (button, "release"), response));
}

bool SoundTouchClient::selectPreset (int preset, int gapMs)
{
    std::vector<int> buttons;

    for (int value = preset; value != 0; value /= 10)
    {
        const int digit = value % 10;

        if (digit < 1 || digit > 6)
        {
            Say (std::cerr) << "Error: '" << preset << "' contains " << digit
                            << ", which is not a button (1-6)\n";
            return (false);
        }

        buttons.insert (buttons.begin (), digit);
    }

    if (buttons.empty ())
    {
        Say (std::cerr) << "Error: preset required\n";
        return (false);
    }

    Say () << "Pressing";

    for (const int button : buttons)
    {
        Say () << " " << button;
    }

    Say () << " for preset " << preset << "...\n";

    for (size_t i = 0; i < buttons.size (); ++i)
    {
        if (i != 0)
        {
            // Has to land inside the listener's combo window or it reads as separate presses.
            std::this_thread::sleep_for (std::chrono::milliseconds (gapMs));
        }

        if (!pressButton (buttons[i]))
        {
            Say (std::cerr) << "Failed to send button " << buttons[i] << "\n";
            return (false);
        }
    }

    return (true);
}
