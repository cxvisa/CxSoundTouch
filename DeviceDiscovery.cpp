#include "DeviceDiscovery.h"
#include "SpeakerConfig.h"
#include "StreamConfig.h"
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <sstream>
#include <fstream>
#include <curl/curl.h>
#include <pugixml.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

DeviceDiscovery::DeviceDiscovery ()
{
}

DeviceDiscovery::~DeviceDiscovery ()
{
}

namespace
{
    // Where the search goes: the SSDP multicast group, or the addresses CXSTCC_SSDP_TARGETS lists.
    std::vector<struct sockaddr_in> searchTargets ()
    {
        std::vector<struct sockaddr_in> targets;
        const char *configured = std::getenv ("CXSTCC_SSDP_TARGETS");
        const std::string list = (configured != nullptr && *configured != '\0') ? configured : "239.255.255.250:1900";
        std::istringstream items (list);
        std::string item;

        while (std::getline (items, item, ','))
        {
            const size_t colon = item.rfind (':');
            const std::string host = item.substr (0, colon);
            const int port = (colon == std::string::npos) ? 1900 : std::atoi (item.c_str () + colon + 1);
            struct sockaddr_in addr;

            std::memset (&addr, 0, sizeof (addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons (static_cast<uint16_t> (port));

            if (port > 0 && port < 65536 && inet_pton (AF_INET, host.c_str (), &addr.sin_addr) == 1)
            {
                targets.push_back (addr);
            }
            else
            {
                std::cerr << "Warning: CXSTCC_SSDP_TARGETS: '" << item << "' is not an address:port\n";
            }
        }

        return (targets);
    }
}

bool DeviceDiscovery::sendMSearch (int sock)
{
    const char *msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
        "\r\n";

    bool sentAny = false;

    for (const struct sockaddr_in &addr : searchTargets ())
    {
        const ssize_t sent = sendto (sock, msearch, std::strlen (msearch), 0,
                                     reinterpret_cast<const struct sockaddr *> (&addr), sizeof (addr));

        sentAny = sentAny || (sent >= 0);
    }

    if (!sentAny)
    {
        std::cerr << "Failed to send M-SEARCH\n";
        return (false);
    }

    return (true);
}

bool DeviceDiscovery::parseResponse (const std::string &response, SoundTouchDevice &device)
{
    std::istringstream iss (response);
    std::string line;

    while (std::getline (iss, line))
    {
        if (line.empty () || line == "\r")
        {
            continue;
        }

        const size_t colonPos = line.find (':');

        if (colonPos == std::string::npos)
        {
            continue;
        }

        std::string key = line.substr (0, colonPos);
        std::string value = line.substr (colonPos + 1);

        while (!value.empty () && (value[0] == ' ' || value[0] == '\t'))
        {
            value = value.substr (1);
        }

        while (!value.empty () && (value.back () == '\r' || value.back () == '\n'))
        {
            value.pop_back ();
        }

        if (key == "LOCATION" || key == "Location")
        {
            device.location = value;

            const size_t httpPos = value.find ("http://");

            if (httpPos != std::string::npos)
            {
                const size_t startPos = httpPos + 7;
                const size_t endPos = value.find (':', startPos);

                if (endPos != std::string::npos)
                {
                    device.ipAddress = value.substr (startPos, endPos - startPos);
                }
            }
        }
        else if (key == "USN" || key == "Usn")
        {
            device.usn = value;
        }
    }

    return (!device.ipAddress.empty ());
}

static size_t curlWriteCallbackStatic (void *contents, size_t size, size_t nmemb, void *userp)
{
    const size_t totalSize = size * nmemb;
    std::string *response = static_cast<std::string *> (userp);

    response->append (static_cast<char *> (contents), totalSize);

    return (totalSize);
}

static int curlCancelCallback (void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    const std::atomic<bool> *cancel = static_cast<const std::atomic<bool> *> (clientp);

    return ((cancel != nullptr && cancel->load ()) ? 1 : 0);
}

bool DeviceDiscovery::queryDeviceInfo (SoundTouchDevice &device, const std::atomic<bool> *cancel)
{
    const std::string url = "http://" + device.ipAddress + ":8090/info";

    CURL *curl = curl_easy_init ();

    if (curl == nullptr)
    {
        return (false);
    }

    std::string response;

    curl_easy_setopt (curl, CURLOPT_URL, url.c_str ());
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, curlWriteCallbackStatic);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L);

    if (cancel != nullptr)
    {
        curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION, curlCancelCallback);
        curl_easy_setopt (curl, CURLOPT_XFERINFODATA, cancel);
        curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);
    }

    const CURLcode res = curl_easy_perform (curl);

    curl_easy_cleanup (curl);

    if (res != CURLE_OK)
    {
        return (false);
    }

    pugi::xml_document doc;

    const pugi::xml_parse_result result = doc.load_string (response.c_str ());

    if (!result)
    {
        return (false);
    }

    const pugi::xml_node infoNode = doc.select_node ("//info").node ();

    if (!infoNode)
    {
        return (false);
    }

    // deviceID is an attribute of <info>, not a child element.
    device.deviceId = infoNode.attribute ("deviceID").value ();
    device.deviceName = infoNode.child ("name").child_value ();
    device.deviceType = infoNode.child ("type").child_value ();

    return (!device.deviceId.empty ());
}

bool DeviceDiscovery::receiveResponses (int sock, int timeoutSeconds, const std::atomic<bool> *cancel)
{
    // Every answer that comes within timeoutSeconds of the search, however many there are: a speaker
    // that answers over and over cannot keep the search going.
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (timeoutSeconds);
    std::vector<SoundTouchDevice> answered;

    auto cancelled = [cancel] () { return (cancel != nullptr && cancel->load ()); };

    char buffer[2048];

    while (!cancelled ())
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds> (deadline - std::chrono::steady_clock::now ());

        if (left.count () <= 0)
        {
            break;
        }

        // In slices, so a cancel is seen within one.
        const long long slice = (cancel != nullptr) ? std::min<long long> (left.count (), 100) : left.count ();
        struct pollfd ready = { sock, POLLIN, 0 };
        const int polled = poll (&ready, 1, static_cast<int> (slice));

        if (polled < 0 && errno == EINTR)
        {
            continue;
        }

        if (polled == 0 && slice < left.count ())
        {
            continue;
        }

        if (polled <= 0)
        {
            break;
        }

        const ssize_t received = recv (sock, buffer, sizeof (buffer) - 1, 0);

        if (received < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                continue;
            }

            break;
        }

        const std::string response (buffer, static_cast<size_t> (received));

        SoundTouchDevice device;

        if (parseResponse (response, device))
        {
            bool duplicate = false;

            for (const auto &existing : answered)
            {
                if (existing.ipAddress == device.ipAddress)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
            {
                answered.push_back (device);
            }
        }
    }

    // Asked for their names only once the answers are in, so a slow speaker cannot make the search
    // miss the others' answers.
    for (SoundTouchDevice &device : answered)
    {
        if (cancelled ())
        {
            break;
        }

        queryDeviceInfo (device, cancel);
        m_devices.push_back (device);
    }

    return (!m_devices.empty ());
}

bool DeviceDiscovery::discover (int timeoutSeconds, bool quiet, const std::atomic<bool> *cancel)
{
    m_devices.clear ();

    const int sock = socket (AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        std::cerr << "Failed to create socket\n";
        return (false);
    }

    if (!sendMSearch (sock))
    {
        close (sock);
        return (false);
    }

    if (!quiet)
    {
        std::cout << "Searching for SoundTouch devices";
        std::cout.flush ();
    }

    receiveResponses (sock, timeoutSeconds, cancel);

    close (sock);

    if (!quiet)
    {
        std::cout << " found " << m_devices.size () << " device(s)\n";
    }

    return (!m_devices.empty ());
}

const SoundTouchDevice *DeviceDiscovery::getFirstDevice () const
{
    if (m_devices.empty ())
    {
        return (nullptr);
    }

    return (&m_devices[0]);
}

void DeviceDiscovery::printDevices () const
{
    if (m_devices.empty ())
    {
        std::cout << "No devices found\n";
        return;
    }

    std::cout << "\nDiscovered SoundTouch Devices:\n";
    std::cout << std::string (60, '=') << "\n";

    for (size_t i = 0; i < m_devices.size (); ++i)
    {
        const SoundTouchDevice &device = m_devices[i];

        std::cout << "Device " << (i + 1) << ":\n";
        std::cout << "  IP Address: " << device.ipAddress << "\n";

        if (!device.deviceName.empty ())
        {
            std::cout << "  Name: " << device.deviceName << "\n";
        }

        if (!device.deviceId.empty ())
        {
            std::cout << "  Device ID: " << device.deviceId << "\n";
        }

        std::cout << "\n";
    }

    std::cout << std::string (60, '=') << "\n";
}

bool DeviceDiscovery::saveToFile (const std::string &filename, const std::string &defaultDeviceId) const
{
    // The speakers just found replace those saved; whatever else the file holds is kept.
    std::string existing;

    {
        std::ifstream ifs (filename);

        if (ifs.is_open ())
        {
            existing.assign (std::istreambuf_iterator<char> (ifs), std::istreambuf_iterator<char> ());
        }
    }

    SpeakerConfig speakers;

    for (const auto &device : m_devices)
    {
        speakers.remember (device);
    }

    const std::string chosen = (defaultDeviceId.empty () && !m_devices.empty ()) ? m_devices[0].deviceId : defaultDeviceId;

    speakers.setDefault (chosen);

    std::string error;

    if (!StreamConfig::saveFile (filename, speakers.fileText (existing), existing, error))
    {
        std::cerr << "Error: " << error << "\n";
        return (false);
    }

    std::cout << "Saved " << m_devices.size () << " device(s) to " << filename << "\n";

    if (!speakers.getDefaultId ().empty ())
    {
        std::cout << "Default device: " << speakers.getDefaultId () << "\n";
    }

    return (true);
}

bool DeviceDiscovery::loadFromFile (const std::string &filename)
{
    std::ifstream ifs (filename);

    if (!ifs.is_open ())
    {
        return (false);
    }

    try
    {
        json j;
        ifs >> j;

        m_devices.clear ();
        m_defaultDeviceId = j.value ("default_device", "");

        if (!j.contains ("devices") || !j["devices"].is_array ())
        {
            return (false);
        }

        for (const auto &item : j["devices"])
        {
            SoundTouchDevice device;

            device.deviceId = item.value ("device_id", "");
            device.deviceName = item.value ("device_name", "");
            device.ipAddress = item.value ("ip_address", "");
            device.deviceType = item.value ("device_type", "");
            device.location = item.value ("location", "");
            device.usn = item.value ("usn", "");

            if (!device.deviceId.empty () && !device.ipAddress.empty ())
            {
                m_devices.push_back (device);
            }
        }

        return (true);
    }
    catch (const json::exception &)
    {
        return (false);
    }
    catch (const std::exception &e)
    {
        // Such as a directory where the file should be, as a docker bind mount of a missing file makes.
        std::cerr << "Error: cannot read devices.json: " << e.what () << "\n";
        return (false);
    }
}

const SoundTouchDevice *DeviceDiscovery::getDefaultDevice () const
{
    if (m_defaultDeviceId.empty ())
    {
        return (getFirstDevice ());
    }

    return (findByDeviceId (m_defaultDeviceId));
}

const SoundTouchDevice *DeviceDiscovery::findByDeviceId (const std::string &deviceId) const
{
    for (const auto &device : m_devices)
    {
        if (device.deviceId == deviceId)
        {
            return (&device);
        }
    }

    return (nullptr);
}

bool DeviceDiscovery::setDefaultDevice (const std::string &deviceId)
{
    if (findByDeviceId (deviceId) == nullptr)
    {
        return (false);
    }

    m_defaultDeviceId = deviceId;

    return (true);
}
