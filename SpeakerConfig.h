#ifndef SPEAKER_CONFIG_H
#define SPEAKER_CONFIG_H

#include "DeviceDiscovery.h"
#include <string>
#include <vector>

// The speakers this program knows, as devices.json keeps them: each speaker found on the network
// and saved, and which of them is the default, the one every command drives. It is the one place
// that reads and writes that file's text, and it keeps whatever else the file holds (such as the
// groups of speakers a later version may play to together), so writing it never loses them.
//
// Free of sockets and of discovery itself, so it can be unit-tested on hand-built speakers.
class SpeakerConfig
{
    public :

        SpeakerConfig ();

        // Takes the speakers from the text of devices.json. False, leaving this empty, for text that
        // is not such a file; an empty or missing list of speakers is fine.
        bool loadFromText (const std::string &text);

        const std::vector<SoundTouchDevice> &getSpeakers () const { return (m_speakers); }

        // The default as the file names it, "" for none.
        const std::string &getDefaultId () const { return (m_defaultId); }

        // The default speaker: the one the file names, else the first, as every command takes it.
        // nullptr when there is none.
        const SoundTouchDevice *getDefaultSpeaker () const;

        const SoundTouchDevice *findById (const std::string &deviceId) const;

        // Adds a speaker found on the network, or brings a saved one up to date: its address, name
        // and model, matched by its device ID. False for a speaker with no ID, which is not taken.
        bool remember (const SoundTouchDevice &speaker);

        // Makes a saved speaker the default. False, changing nothing, for an ID not saved.
        bool setDefault (const std::string &deviceId);

        // The text of devices.json for these speakers. What existing (the file as it was, "" for
        // none) holds besides is kept as it was: any other setting, the order of its keys, and any
        // field this program does not know on a speaker that keeps its ID.
        std::string fileText (const std::string &existing) const;

    private :

        // Now the data members

        std::vector<SoundTouchDevice> m_speakers;
        std::string                   m_defaultId;
};

#endif
