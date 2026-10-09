#include "StreamConfig.h"
#include <nlohmann/json.hpp>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>

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

bool StreamConfig::loadFromFile (const std::string &filename, bool quiet)
{
    std::ifstream ifs (filename);

    if (!ifs.is_open ())
    {
        std::cerr << "Error: Could not open " << filename << "\n";
        return (false);
    }

    std::string text;

    try
    {
        text.assign (std::istreambuf_iterator<char> (ifs), std::istreambuf_iterator<char> ());
    }
    catch (const std::exception &e)
    {
        // Such as a directory where the file should be, as a docker bind mount of a missing file makes.
        std::cerr << "Error: cannot read " << filename << ": " << e.what () << "\n";
        return (false);
    }

    return (loadFromText (text, quiet, filename));
}

bool StreamConfig::loadFromText (const std::string &text, bool quiet, const std::string &origin)
{
    try
    {
        json j;
        std::istringstream iss (text);

        iss >> j;

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

        if (!quiet)
        {
            std::cout << "Loaded " << m_streams.size () << " streams from " << origin << "\n";
        }

        return (true);
    }
    catch (const json::exception &e)
    {
        std::cerr << "JSON parsing error: " << e.what () << "\n";
        return (false);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: cannot read " << origin << ": " << e.what () << "\n";
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

bool StreamConfig::isValidName (const std::string &name)
{
    if (name.empty () || name.size () > MAX_NAME)
    {
        return (false);
    }

    for (const char c : name)
    {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = (c >= '0' && c <= '9');

        if (!letter && !digit && c != '.' && c != '_' && c != '-')
        {
            return (false);
        }
    }

    return (true);
}

bool StreamConfig::isValidUrl (const std::string &url)
{
    if (url.size () > MAX_URL)
    {
        return (false);
    }

    for (const char c : url)
    {
        const unsigned char byte = static_cast<unsigned char> (c);

        if (byte <= 0x20 || byte == 0x7F)
        {
            return (false);
        }
    }

    size_t start = 0;

    for (const char *scheme : { "http://", "https://" })
    {
        const size_t length = std::strlen (scheme);

        if (url.size () > length && strncasecmp (url.c_str (), scheme, length) == 0)
        {
            start = length;
            break;
        }
    }

    if (start == 0)
    {
        return (false);
    }

    // The host is what comes before the path, the query or the fragment, after any user:password@.
    const size_t end = url.find_first_of ("/?#", start);
    std::string host = url.substr (start, (end == std::string::npos) ? std::string::npos : end - start);
    const size_t at = host.rfind ('@');

    if (at != std::string::npos)
    {
        host.erase (0, at + 1);
    }

    return (!host.empty () && host[0] != ':');
}

std::string StreamConfig::fileText (const std::vector<Stream> &streams, const std::string &existing)
{
    using ordered = nlohmann::ordered_json;

    ordered document = ordered::object ();

    if (!existing.empty ())
    {
        ordered old = ordered::parse (existing, nullptr, false);

        if (!old.is_discarded () && old.is_object ())
        {
            document = std::move (old);
        }
    }

    if (document.empty ())
    {
        document["combo_window_ms"] = 700;
    }

    // What each stream had besides the fields below, by name.
    std::map<std::string, ordered> extras;
    const auto known = { "name", "display_name", "description", "url", "preset" };
    const auto oldStreams = document.find ("streams");

    if (oldStreams != document.end () && oldStreams->is_array ())
    {
        for (const ordered &item : *oldStreams)
        {
            const auto name = item.is_object () ? item.find ("name") : item.end ();

            if (name == item.end () || !name->is_string ())
            {
                continue;
            }

            ordered kept = ordered::object ();

            for (auto field = item.begin (); field != item.end (); ++field)
            {
                if (std::find (known.begin (), known.end (), field.key ()) == known.end ())
                {
                    kept[field.key ()] = field.value ();
                }
            }

            extras[name->get<std::string> ()] = std::move (kept);
        }
    }

    ordered list = ordered::array ();

    for (const Stream &stream : streams)
    {
        ordered item = ordered::object ();

        item["name"] = stream.name;
        item["display_name"] = stream.displayName;

        if (!stream.description.empty ())
        {
            item["description"] = stream.description;
        }

        item["url"] = stream.url;

        if (stream.preset != 0)
        {
            item["preset"] = stream.preset;
        }

        const auto kept = extras.find (stream.name);

        if (kept != extras.end ())
        {
            for (auto field = kept->second.begin (); field != kept->second.end (); ++field)
            {
                item[field.key ()] = field.value ();
            }
        }

        list.push_back (std::move (item));
    }

    // Assigned in place, so the key keeps its position among the rest.
    document["streams"] = std::move (list);

    return (document.dump (2, ' ', false, ordered::error_handler_t::replace) + "\n");
}

namespace
{
    // Writes all of text to path, created or emptied, with mode for a file it creates, and flushes it
    // to the disk. 0, or the errno that stopped it.
    int writeWholeFile (const std::string &path, const std::string &text, mode_t mode)
    {
        const int fd = open (path.c_str (), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);

        if (fd < 0)
        {
            return (errno);
        }

        const char *data = text.data ();
        size_t left = text.size ();
        int failure = 0;

        while (left > 0 && failure == 0)
        {
            const ssize_t wrote = write (fd, data, left);

            if (wrote < 0)
            {
                failure = (errno == EINTR) ? 0 : errno;
                continue;
            }

            data += wrote;
            left -= static_cast<size_t> (wrote);
        }

        if (failure == 0 && fsync (fd) != 0)
        {
            failure = errno;
        }

        if (close (fd) != 0 && failure == 0)
        {
            failure = errno;
        }

        return (failure);
    }

    // Makes text the contents of path: written beside it, then renamed over it. A file that is a mount
    // point of its own cannot be renamed over, and a directory that cannot be written has no room for
    // the temporary file, but the file itself may still be writable: then it is rewritten in place.
    // 0, or the errno that stopped it.
    int replaceFile (const std::string &path, const std::string &text)
    {
        struct stat was;
        const mode_t mode = (stat (path.c_str (), &was) == 0) ? (was.st_mode & 07777) : 0644;
        const std::string temporary = path + ".tmp";
        int failure = writeWholeFile (temporary, text, mode);

        if (failure == 0)
        {
            // open() applies the umask to a new file; the old file's mode carries over as it was.
            chmod (temporary.c_str (), mode);

            if (rename (temporary.c_str (), path.c_str ()) == 0)
            {
                return (0);
            }

            failure = errno;
        }

        unlink (temporary.c_str ());

        if (failure == EBUSY || failure == EXDEV || failure == EACCES || failure == EPERM || failure == EROFS)
        {
            failure = writeWholeFile (path, text, mode);
        }

        return (failure);
    }
}

bool StreamConfig::saveFile (const std::string &filename, const std::string &text, const std::string &previous,
                             std::string &error)
{
    // Only a convenience, so a backup that cannot be written does not stop the save.
    if (!previous.empty ())
    {
        const int failure = replaceFile (filename + ".bak", previous);

        if (failure != 0)
        {
            std::cerr << "Warning: cannot keep the previous " << filename << " as " << filename << ".bak: "
                      << std::strerror (failure) << "\n";
        }
    }

    const int failure = replaceFile (filename, text);

    if (failure != 0)
    {
        error = std::string ("cannot write ") + filename + ": " + std::strerror (failure);
        return (false);
    }

    return (true);
}
