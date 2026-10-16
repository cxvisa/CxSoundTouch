#include "HttpUtil.h"
#include <cctype>
#include <charconv>
#include <sstream>
#include <system_error>

std::string HttpUtil::firstLine (const std::string &head)
{
    const size_t end = head.find ('\n');

    if (end == std::string::npos)
    {
        return (head);
    }

    // Drop a trailing '\r' from a CRLF line ending.
    size_t last = end;

    if (last > 0 && head[last - 1] == '\r')
    {
        --last;
    }

    return (head.substr (0, last));
}

bool HttpUtil::parseRequestLine (const std::string &line, HttpRequest &request)
{
    std::istringstream stream (line);
    std::string method;
    std::string target;
    std::string version;
    std::string extra;

    if (!(stream >> method >> target >> version))
    {
        return (false);
    }

    // A fourth token means the line was not a well-formed request line.
    if (stream >> extra)
    {
        return (false);
    }

    if (target.empty () || target[0] != '/' || version.compare (0, 5, "HTTP/") != 0)
    {
        return (false);
    }

    request.method = method;
    request.version = version;
    request.target = target;

    const size_t mark = target.find ('?');

    if (mark == std::string::npos)
    {
        request.path = target;
        request.query.clear ();
    }
    else
    {
        request.path = target.substr (0, mark);
        request.query = target.substr (mark + 1);
    }

    return (true);
}

std::string HttpUtil::percentDecode (const std::string &text)
{
    std::string decoded;

    decoded.reserve (text.size ());

    for (size_t i = 0; i < text.size (); ++i)
    {
        if (text[i] == '+')
        {
            decoded += ' ';
        }
        else if (text[i] == '%' && i + 2 < text.size ()
                 && std::isxdigit (static_cast<unsigned char> (text[i + 1]))
                 && std::isxdigit (static_cast<unsigned char> (text[i + 2])))
        {
            decoded += static_cast<char> (std::stoi (text.substr (i + 1, 2), nullptr, 16));
            i += 2;
        }
        else
        {
            decoded += text[i];
        }
    }

    return (decoded);
}

std::string HttpUtil::queryValue (const std::string &query, const std::string &key)
{
    size_t at = 0;

    while (at < query.size ())
    {
        const size_t amp = query.find ('&', at);
        const std::string pair = query.substr (at, (amp == std::string::npos) ? std::string::npos : amp - at);
        const size_t equals = pair.find ('=');

        if (equals != std::string::npos && pair.substr (0, equals) == key)
        {
            return (percentDecode (pair.substr (equals + 1)));
        }

        if (amp == std::string::npos)
        {
            break;
        }

        at = amp + 1;
    }

    return (std::string ());
}

bool HttpUtil::parseUnsigned (const std::string &text, std::uint64_t &value)
{
    if (text.empty ())
    {
        return (false);
    }

    std::uint64_t parsed = 0;
    const char *end = text.data () + text.size ();
    const std::from_chars_result result = std::from_chars (text.data (), end, parsed);

    if (result.ec != std::errc () || result.ptr != end)
    {
        return (false);
    }

    value = parsed;

    return (true);
}

namespace
{
    std::string trimmed (const std::string &text)
    {
        const size_t first = text.find_first_not_of (" \t");

        if (first == std::string::npos)
        {
            return (std::string ());
        }

        return (text.substr (first, text.find_last_not_of (" \t") - first + 1));
    }

    std::string lowerCased (std::string text)
    {
        for (char &c : text)
        {
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        }

        return (text);
    }
}

std::string HttpUtil::headerValue (const std::string &head, const std::string &name)
{
    const std::string wanted = lowerCased (name);
    size_t at = head.find ('\n');

    // The first line is the request line, never a header.
    while (at != std::string::npos)
    {
        const size_t start = at + 1;
        const size_t end = head.find ('\n', start);
        std::string line = head.substr (start, (end == std::string::npos) ? std::string::npos : end - start);

        if (!line.empty () && line.back () == '\r')
        {
            line.pop_back ();
        }

        const size_t colon = line.find (':');

        if (colon != std::string::npos && lowerCased (trimmed (line.substr (0, colon))) == wanted)
        {
            return (trimmed (line.substr (colon + 1)));
        }

        at = end;
    }

    return (std::string ());
}

std::string HttpUtil::mediaType (const std::string &contentType)
{
    return (lowerCased (trimmed (contentType.substr (0, contentType.find (';')))));
}

std::string HttpUtil::entityTag (const std::string &bytes)
{
    // 64-bit FNV-1a: quick, and plenty to tell one version of a small file from another.
    std::uint64_t hash = 0xcbf29ce484222325ULL;

    for (const char c : bytes)
    {
        hash ^= static_cast<unsigned char> (c);
        hash *= 0x100000001b3ULL;
    }

    static const char digits[] = "0123456789abcdef";
    std::string tag = "\"";

    for (int shift = 60; shift >= 0; shift -= 4)
    {
        tag += digits[(hash >> shift) & 0xF];
    }

    tag += "-" + std::to_string (bytes.size ()) + "\"";

    return (tag);
}

bool HttpUtil::ifMatches (const std::string &ifMatch, const std::string &tag)
{
    const std::string wanted = trimmed (ifMatch);

    if (wanted == "*")
    {
        return (true);
    }

    size_t start = 0;

    while (start <= wanted.size ())
    {
        const size_t comma = wanted.find (',', start);
        const std::string one = trimmed (wanted.substr (start, (comma == std::string::npos) ? std::string::npos : comma - start));

        if (!one.empty () && one == tag)
        {
            return (true);
        }

        if (comma == std::string::npos)
        {
            break;
        }

        start = comma + 1;
    }

    return (false);
}
