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

        // As loadFromFile, from a streams.json already read, such as one a save has just written;
        // origin names it in messages.
        bool loadFromText (const std::string &text, bool quiet = true, const std::string &origin = "streams.json");

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

        // The rules a stream list saved from the dashboard is held to; the loader, which has to take
        // whatever a hand-edited file holds, is more forgiving. A name is what `play` and the relay's
        // URLs know a stream by: letters, digits, '.', '_' and '-'. A URL is http:// or https:// with
        // a host, and has no spaces or control characters in it.
        static constexpr size_t MAX_NAME = 64;
        static constexpr size_t MAX_DISPLAY_NAME = 100;
        static constexpr size_t MAX_DESCRIPTION = 500;
        static constexpr size_t MAX_URL = 2048;

        static bool isValidName (const std::string &name);
        static bool isValidUrl (const std::string &url);

        // The text of a streams.json holding these streams, in this order. Everything else in existing,
        // the file as it was ("" for none), is kept as it was: combo_window_ms and any other setting,
        // the order of its keys, and any field this program does not know on a stream that keeps its
        // name. A new file gets combo_window_ms at its default.
        static std::string fileText (const std::vector<Stream> &streams, const std::string &existing);

        // Makes text the contents of filename, keeping previous (what it held, "" for nothing) as
        // filename.bak. The text is written beside it and renamed over it, so nothing ever reads half
        // a file; where that cannot be done, as with a file bind-mounted on its own, it is rewritten
        // in place. False, with the reason in error, when it could not be written.
        static bool saveFile (const std::string &filename, const std::string &text, const std::string &previous,
                              std::string &error);

    private :

        // Now the data members

        std::vector<Stream>           m_streams;
        std::map<std::string, size_t> m_nameIndex;
        std::map<int, size_t>         m_presetIndex;
        int                           m_comboWindowMs = 700;
};

#endif
