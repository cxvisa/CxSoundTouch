#include "StreamConfig.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <set>

using json = nlohmann::json;

StreamConfig::StreamConfig ()
{
}

StreamConfig::~StreamConfig ()
{
}

int StreamConfig::digitCount (int preset)
{
    int count = 0;

    for (int value = preset; value != 0; value /= 10)
    {
        ++count;
    }

    return (count);
}

bool StreamConfig::isValidPreset (int preset)
{
    if (preset < 1 || digitCount (preset) > MAX_COMBO_DIGITS)
    {
        return (false);
    }

    // Every digit has to name a real button, so 0 and 7-9 are out.
    for (int value = preset; value != 0; value /= 10)
    {
        const int digit = value % 10;

        if (digit < 1 || digit > MAX_BUTTON)
        {
            return (false);
        }
    }

    return (true);
}

namespace
{
    bool presetUsesButton (int preset, int button)
    {
        for (int value = preset; value != 0; value /= 10)
        {
            if ((value % 10) == button)
            {
                return (true);
            }
        }

        return (false);
    }
}

std::vector<int> StreamConfig::getRequiredButtons () const
{
    std::set<int> buttons;

    for (const auto &entry : m_presetIndex)
    {
        for (int value = entry.first; value != 0; value /= 10)
        {
            buttons.insert (value % 10);
        }
    }

    return (std::vector<int> (buttons.begin (), buttons.end ()));
}

const Stream *StreamConfig::findStreamUsingButton (int button) const
{
    for (const auto &entry : m_presetIndex)
    {
        if (presetUsesButton (entry.first, button))
        {
            return (&m_streams[entry.second]);
        }
    }

    return (nullptr);
}

bool StreamConfig::hasLongerPresetStartingWith (int sequence) const
{
    const int sequenceLength = digitCount (sequence);

    for (const auto &entry : m_presetIndex)
    {
        const int extra = digitCount (entry.first) - sequenceLength;

        if (extra <= 0)
        {
            continue;
        }

        int prefix = entry.first;

        for (int i = 0; i < extra; ++i)
        {
            prefix /= 10;
        }

        if (prefix == sequence)
        {
            return (true);
        }
    }

    return (false);
}

bool StreamConfig::loadFromFile (const std::string &filename)
{
    std::ifstream ifs (filename);

    if (!ifs.is_open ())
    {
        std::cerr << "Error: Could not open " << filename << "\n";
        return (false);
    }

    try
    {
        json j;
        ifs >> j;

        m_streams.clear ();
        m_nameIndex.clear ();
        m_presetIndex.clear ();

        m_comboWindowMs = j.value ("combo_window_ms", 700);

        if (!j.contains ("streams") || !j["streams"].is_array ())
        {
            std::cerr << "Error: Invalid JSON format - missing 'streams' array\n";
            return (false);
        }

        for (const auto &item : j["streams"])
        {
            Stream stream;

            stream.name = item.value ("name", "");
            stream.displayName = item.value ("display_name", "");
            stream.description = item.value ("description", "");
            stream.url = item.value ("url", "");
            stream.preset = item.value ("preset", 0);

            if (stream.name.empty () || stream.url.empty ())
            {
                std::cerr << "Warning: Skipping stream with missing name or url\n";
                continue;
            }

            if (stream.preset != 0 && !isValidPreset (stream.preset))
            {
                std::cerr << "Warning: stream '" << stream.name << "' has preset "
                          << stream.preset << ", which is not a button (1-" << MAX_BUTTON
                          << ") or a two-digit combo of buttons; ignoring the mapping\n";

                stream.preset = 0;
            }

            const size_t index = m_streams.size ();

            m_streams.push_back (stream);
            m_nameIndex[stream.name] = index;

            if (stream.preset != 0)
            {
                m_presetIndex[stream.preset] = index;
            }
        }

        std::cout << "Loaded " << m_streams.size () << " streams from " << filename << "\n";

        return (true);
    }
    catch (const json::exception &e)
    {
        std::cerr << "JSON parsing error: " << e.what () << "\n";
        return (false);
    }
    catch (const std::exception &e)
    {
        // Such as a directory where the file should be, as a docker bind mount of a missing file makes.
        std::cerr << "Error: cannot read " << filename << ": " << e.what () << "\n";
        return (false);
    }
}

const Stream *StreamConfig::findByName (const std::string &name) const
{
    const auto it = m_nameIndex.find (name);

    if (it == m_nameIndex.end ())
    {
        return (nullptr);
    }

    return (&m_streams[it->second]);
}

const Stream *StreamConfig::findByPreset (int presetId) const
{
    const auto it = m_presetIndex.find (presetId);

    if (it == m_presetIndex.end ())
    {
        return (nullptr);
    }

    return (&m_streams[it->second]);
}

std::vector<const Stream *> StreamConfig::getPresetStreams () const
{
    std::vector<const Stream *> ordered;

    for (const auto &entry : m_presetIndex)
    {
        ordered.push_back (&m_streams[entry.second]);
    }

    return (ordered);
}

const Stream *StreamConfig::findByUrl (const std::string &url) const
{
    if (url.empty ())
    {
        return (nullptr);
    }

    for (const auto &stream : m_streams)
    {
        if (stream.url == url)
        {
            return (&stream);
        }
    }

    return (nullptr);
}

void StreamConfig::listStreams () const
{
    std::cout << "\nAvailable Streams:\n";
    std::cout << std::string (60, '=') << "\n";

    for (const auto &stream : m_streams)
    {
        std::cout << "  " << stream.name;

        if (stream.preset != 0)
        {
            std::cout << " (preset " << stream.preset << ")";
        }

        std::cout << "\n";
        std::cout << "    " << stream.displayName;

        if (!stream.description.empty () && stream.description != stream.displayName)
        {
            std::cout << " - " << stream.description;
        }

        std::cout << "\n";
    }

    std::cout << std::string (60, '=') << "\n";
}
