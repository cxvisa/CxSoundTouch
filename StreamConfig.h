#ifndef STREAM_CONFIG_H
#define STREAM_CONFIG_H

#include <string>
#include <vector>
#include <map>

struct Stream
{
    std::string name;
    std::string displayName;
    std::string description;
    std::string url;
    int preset;

    Stream ()
        : preset (0)
    {
    }
};

class StreamConfig
{
    public :

        StreamConfig ();
        ~StreamConfig ();

        bool loadFromFile (const std::string &filename, bool quiet = false);
        const Stream *findByName (const std::string &name) const;
        const Stream *findByPreset (int presetId) const;
        const Stream *findByUrl (const std::string &url) const;
        void listStreams () const;

        const std::vector<Stream> &getStreams () const { return (m_streams); }

        // Streams that declare a preset, ordered by preset number.
        std::vector<const Stream *> getPresetStreams () const;

        // True when some preset is longer than this digit sequence and begins with it, meaning we
        // must wait to see whether another digit follows.
        bool hasLongerPresetStartingWith (int sequence) const;

        // Every button the configuration depends on, whether as a preset of its own or as either
        // digit of a combo. The speaker only reports a press for a button that has something
        // stored, so all of these need to be programmed.
        std::vector<int> getRequiredButtons () const;

        // Any stream whose preset uses this button, for use as placeholder content on a button
        // that has no preset of its own.
        const Stream *findStreamUsingButton (int button) const;

        int getComboWindowMs () const { return (m_comboWindowMs); }

        // Physical buttons on the speaker; longer presets are combos we resolve ourselves.
        static constexpr int MAX_BUTTON = 6;
        static constexpr int MAX_COMBO_DIGITS = 3;

        static bool isValidPreset (int preset);
        static int  digitCount (int preset);

    private :

        // Now the data members

        std::vector<Stream>           m_streams;
        std::map<std::string, size_t> m_nameIndex;
        std::map<int, size_t>         m_presetIndex;
        int                           m_comboWindowMs = 700;
};

#endif
