#include "DeviceDiscovery.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
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

bool DeviceDiscovery::sendMSearch (int sock)
{
    const char *msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
        "\r\n";

    struct sockaddr_in addr;
    std::memset (&addr, 0, sizeof (addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons (1900);
    addr.sin_addr.s_addr = inet_addr ("239.255.255.250");

    const ssize_t sent = sendto (sock, msearch, std::strlen (msearch), 0,
                                 reinterpret_cast<struct sockaddr *> (&addr),
                                 sizeof (addr));

    if (sent < 0)
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

bool DeviceDiscovery::queryDeviceInfo (SoundTouchDevice &device)
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

    return (!device.deviceId.empty ());
}

bool DeviceDiscovery::receiveResponses (int sock, int timeoutSeconds)
{
    struct timeval tv;

    tv.tv_sec = timeoutSeconds;
    tv.tv_usec = 0;

    setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv));

    char buffer[2048];
    struct sockaddr_in fromAddr;
    socklen_t fromLen = sizeof (fromAddr);

    while (true)
    {
        std::memset (buffer, 0, sizeof (buffer));

        const ssize_t received = recvfrom (sock, buffer, sizeof (buffer) - 1, 0,
                                           reinterpret_cast<struct sockaddr *> (&fromAddr),
                                           &fromLen);

        if (received < 0)
        {
            break;
        }

        const std::string response (buffer, static_cast<size_t> (received));

        SoundTouchDevice device;

        if (parseResponse (response, device))
        {
            bool duplicate = false;

            for (const auto &existing : m_devices)
            {
                if (existing.ipAddress == device.ipAddress)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
            {
                queryDeviceInfo (device);
                m_devices.push_back (device);
            }
        }
    }

    return (!m_devices.empty ());
}

bool DeviceDiscovery::discover (int timeoutSeconds)
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

    std::cout << "Searching for SoundTouch devices";
    std::cout.flush ();

    receiveResponses (sock, timeoutSeconds);

    close (sock);

    std::cout << " found " << m_devices.size () << " device(s)\n";

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
    json j;

    j["default_device"] = defaultDeviceId.empty () && !m_devices.empty () ? m_devices[0].deviceId : defaultDeviceId;

    json devicesArray = json::array ();

    for (const auto &device : m_devices)
    {
        json deviceJson;

        deviceJson["device_id"] = device.deviceId;
        deviceJson["device_name"] = device.deviceName;
        deviceJson["ip_address"] = device.ipAddress;
        deviceJson["location"] = device.location;
        deviceJson["usn"] = device.usn;

        devicesArray.push_back (deviceJson);
    }

    j["devices"] = devicesArray;

    std::ofstream ofs (filename);

    if (!ofs.is_open ())
    {
        std::cerr << "Error: Could not write to " << filename << "\n";
        return (false);
    }

    ofs << j.dump (2) << "\n";

    std::cout << "Saved " << m_devices.size () << " device(s) to " << filename << "\n";

    if (!j["default_device"].get<std::string> ().empty ())
    {
        std::cout << "Default device: " << j["default_device"].get<std::string> () << "\n";
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
