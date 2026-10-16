#ifndef DEVICE_DISCOVERY_H
#define DEVICE_DISCOVERY_H

#include <atomic>
#include <string>
#include <vector>

struct SoundTouchDevice
{
    std::string ipAddress;
    std::string location;
    std::string usn;
    std::string deviceId;
    std::string deviceName;
    std::string deviceType;     // the model, such as "SoundTouch 30"

    SoundTouchDevice ()
    {
    }
};

class DeviceDiscovery
{
    public :

        DeviceDiscovery ();
        ~DeviceDiscovery ();

        // Asks the network for speakers and waits timeoutSeconds for their answers, then asks each its
        // name. Quiet, it says nothing on stdout, as a search running behind the dashboard must not.
        //
        // The question goes to the SSDP multicast group; CXSTCC_SSDP_TARGETS, a comma-separated list of
        // address:port, sends it there instead, as the tests do to reach a fake speaker on loopback.
        //
        // cancel, when given, ends the search within ~100 ms of becoming true, so a server stopping
        // need not wait the search out.
        bool discover (int timeoutSeconds = 3, bool quiet = false, const std::atomic<bool> *cancel = nullptr);
        const std::vector<SoundTouchDevice> &getDevices () const { return (m_devices); }
        const SoundTouchDevice *getFirstDevice () const;
        void printDevices () const;

        bool saveToFile (const std::string &filename, const std::string &defaultDeviceId = "") const;
        bool loadFromFile (const std::string &filename);
        const SoundTouchDevice *getDefaultDevice () const;
        bool setDefaultDevice (const std::string &deviceId);
        const SoundTouchDevice *findByDeviceId (const std::string &deviceId) const;

    private :

        // Sends the search to each target: true when it went to at least one.
        bool sendMSearch (int sock);
        bool receiveResponses (int sock, int timeoutSeconds, const std::atomic<bool> *cancel);
        bool parseResponse (const std::string &response, SoundTouchDevice &device);
        bool queryDeviceInfo (SoundTouchDevice &device, const std::atomic<bool> *cancel);

        // Now the data members

        std::vector<SoundTouchDevice> m_devices;
        std::string                   m_defaultDeviceId;
};

#endif
