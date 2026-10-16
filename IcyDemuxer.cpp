#include "IcyDemuxer.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

IcyDemuxer::IcyDemuxer (AudioCallback onAudio, TitleCallback onTitle)
    : m_onAudio (onAudio),
      m_onTitle (onTitle),
      m_metaInterval (0),
      m_bitrateKbps (0),
      m_audioLeft (0),
      m_metaLeft (0),
      m_awaitingLength (false)
{
}

void IcyDemuxer::onHeader (const std::string &line)
{
    std::string lower = line;

    std::transform (lower.begin (), lower.end (), lower.begin (),
                    [] (unsigned char c) { return (static_cast<char> (std::tolower (c))); });

    if (lower.compare (0, 5, "http/") == 0 || lower.compare (0, 4, "icy ") == 0)
    {
        m_contentType.clear ();
        m_metaInterval = 0;
        m_bitrateKbps = 0;
        m_audioLeft = 0;
        m_metaLeft = 0;
        m_awaitingLength = false;

        return;
    }

    const size_t colon = line.find (':');

    if (colon == std::string::npos)
    {
        return;
    }

    const std::string key = lower.substr (0, colon);
    std::string value = line.substr (colon + 1);

    const size_t first = value.find_first_not_of (" \t");
    const size_t last = value.find_last_not_of (" \t\r\n");

    value = (first == std::string::npos) ? std::string () : value.substr (first, last - first + 1);

    if (key == "icy-metaint")
    {
        m_metaInterval = std::strtol (value.c_str (), nullptr, 10);
        m_audioLeft = m_metaInterval;
    }
    else if (key == "icy-br")
    {
        // Sometimes a list ("64,64"); the first entry is the stream's.
        m_bitrateKbps = std::strtol (value.c_str (), nullptr, 10);
    }
    else if (key == "content-type")
    {
        m_contentType = value;
    }
}

bool IcyDemuxer::emitTitle ()
{
    // Blocks look like: StreamTitle='Artist - Song';StreamUrl='';  padded with NULs.
    const std::string marker = "StreamTitle='";
    const size_t start = m_metaBuffer.find (marker);

    if (start == std::string::npos)
    {
        return (true);
    }

    const size_t from = start + marker.size ();
    const size_t end = m_metaBuffer.find ("';", from);

    if (end == std::string::npos)
    {
        return (true);
    }

    const std::string title = m_metaBuffer.substr (from, end - from);

    if (title.empty () || title == m_lastTitle)
    {
        return (true);
    }

    m_lastTitle = title;

    return (m_onTitle ? m_onTitle (title) : true);
}

bool IcyDemuxer::onBody (const char *data, size_t length)
{
    if (m_metaInterval <= 0)
    {
        if (m_onAudio)
        {
            m_onAudio (data, length);
        }

        return (true);
    }

    size_t offset = 0;

    while (offset < length)
    {
        const size_t remaining = length - offset;

        if (m_audioLeft > 0)
        {
            const size_t take = std::min (remaining, static_cast<size_t> (m_audioLeft));

            if (m_onAudio)
            {
                m_onAudio (data + offset, take);
            }

            offset += take;
            m_audioLeft -= static_cast<long> (take);

            if (m_audioLeft == 0)
            {
                m_awaitingLength = true;
            }

            continue;
        }

        if (m_awaitingLength)
        {
            m_metaLeft = static_cast<long> (static_cast<unsigned char> (data[offset])) * 16;
            ++offset;
            m_awaitingLength = false;
            m_metaBuffer.clear ();

            if (m_metaLeft == 0)
            {
                m_audioLeft = m_metaInterval;
            }

            continue;
        }

        const size_t take = std::min (remaining, static_cast<size_t> (m_metaLeft));

        m_metaBuffer.append (data + offset, take);
        offset += take;
        m_metaLeft -= static_cast<long> (take);

        if (m_metaLeft == 0)
        {
            m_audioLeft = m_metaInterval;

            if (!emitTitle ())
            {
                return (false);
            }
        }
    }

    return (true);
}
