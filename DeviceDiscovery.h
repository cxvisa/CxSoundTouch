#ifndef DEVICE_DISCOVERY_H
#define DEVICE_DISCOVERY_H

#include <string>
#include <vector>

struct SoundTouchDevice
{
    std::string ipAddress;
    std::string location;
    std::string usn;
    std::string deviceId;
    std::string deviceName;

    SoundTouchDevice ()
    {
    }
};

class DeviceDiscovery
{
    public :

        DeviceDiscovery ();
        ~DeviceDiscovery ();

        bool discover (int timeoutSeconds = 3);
        const std::vector<SoundTouchDevice> &getDevices () const { return (m_devices); }
        const SoundTouchDevice *getFirstDevice () const;
        void printDevices () const;

        bool saveToFile (const std::string &filename, const std::string &defaultDeviceId = "") const;
        bool loadFromFile (const std::string &filename);
        const SoundTouchDevice *getDefaultDevice () const;
        bool setDefaultDevice (const std::string &deviceId);
        const SoundTouchDevice *findByDeviceId (const std::string &deviceId) const;

    private :

        bool sendMSearch (int sock);
        bool receiveResponses (int sock, int timeoutSeconds);
        bool parseResponse (const std::string &response, SoundTouchDevice &device);
        bool queryDeviceInfo (SoundTouchDevice &device);

        // Now the data members

        std::vector<SoundTouchDevice> m_devices;
        std::string                   m_defaultDeviceId;
};

#endif
