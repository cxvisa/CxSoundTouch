#ifndef HTTP_UTIL_H
#define HTTP_UTIL_H

#include <cstdint>
#include <string>

// A parsed HTTP request line, and the pieces of its target a router needs.
struct HttpRequest
{
    std::string method;    // "GET", "HEAD", ...
    std::string target;    // the raw request target, e.g. "/api/streams?station=klove"
    std::string path;      // the target without its query, e.g. "/api/streams"
    std::string query;     // the part after '?', empty when there is none
    std::string version;   // "HTTP/1.1", ...
    std::string contentType;   // the Content-Type header, empty when there is none
    std::string body;          // the body, as long as Content-Length said; empty for a GET

    HttpRequest ()
    {
    }
};

// Small, socket-free helpers for the dashboard's web server, split out so they can be unit-tested on
// their own.
namespace HttpUtil
{
    // The first line of a request head, up to its CRLF (or a lone LF); the whole string when it has
    // neither.
    std::string firstLine (const std::string &head);

    // Parses "METHOD target VERSION". False unless it is exactly three space-separated tokens with a
    // target that starts with '/' and an "HTTP/..." version. Splits the target into path and query
    // at the first '?'.
    bool parseRequestLine (const std::string &line, HttpRequest &request);

    // Percent-decoding; a malformed %XX is left as written.
    std::string percentDecode (const std::string &text);

    // The value of one key in an "a=1&b=2" query, percent-decoded, or "" when the key is absent.
    std::string queryValue (const std::string &query, const std::string &key);

    // A whole unsigned decimal number, as a query value carries one. False, leaving value alone, for
    // anything else: an empty string, a sign, spaces, trailing junk, or more than 64 bits.
    bool parseUnsigned (const std::string &text, std::uint64_t &value);

    // The value of a header in a request head (its request line and header lines, without the blank
    // line that ends it), trimmed; the name matches case-insensitively. "" when it is absent.
    std::string headerValue (const std::string &head, const std::string &name);

    // The media type of a Content-Type, lower-cased and without its parameters:
    // "Application/JSON; charset=utf-8" gives "application/json".
    std::string mediaType (const std::string &contentType);
}

#endif
