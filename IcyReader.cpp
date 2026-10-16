#include "IcyReader.h"
#include <curl/curl.h>

// How long the station may go quiet before the connection is given up on.
static constexpr long STALL_SECONDS = 15;

IcyReader::IcyReader (TitleCallback onTitle, AbortCheck shouldAbort)
    : m_demuxer (IcyDemuxer::AudioCallback (), onTitle),
      m_shouldAbort (shouldAbort),
      m_checkedMetadata (false),
      m_lacksMetadata (false),
      m_stopped (false)
{
}

size_t IcyReader::headerTrampoline (char *buffer, size_t size, size_t items, void *userp)
{
    static_cast<IcyReader *> (userp)->m_demuxer.onHeader (std::string (buffer, size * items));

    return (size * items);
}

size_t IcyReader::bodyTrampoline (char *buffer, size_t size, size_t items, void *userp)
{
    return (static_cast<IcyReader *> (userp)->onBody (buffer, size * items));
}

size_t IcyReader::onBody (const char *buffer, size_t length)
{
    if (!m_checkedMetadata)
    {
        m_checkedMetadata = true;

        if (m_demuxer.getMetaInterval () <= 0)
        {
            // Server sent no metadata, so there is nothing to find in the audio.
            m_error = "stream did not offer metadata (icy-metaint absent)";
            m_lacksMetadata = true;
            return (0);
        }
    }

    if (!m_demuxer.onBody (buffer, length))
    {
        m_stopped = true;
        return (0);
    }

    return (length);
}

bool IcyReader::read (const std::string &streamUrl, long timeoutSeconds)
{
    CURL *curl = curl_easy_init ();

    if (curl == nullptr)
    {
        m_error = "could not initialise curl";
        return (false);
    }

    struct curl_slist *headers = nullptr;

    headers = curl_slist_append (headers, "Icy-MetaData: 1");

    // Shoutcast v1 servers answer "ICY 200 OK", which libcurl otherwise rejects as HTTP/0.9.
    struct curl_slist *aliases = curl_slist_append (nullptr, "ICY 200 OK");

    // libcurl's own account of a failure says which step failed, also behind a redirect.
    char errorText[CURL_ERROR_SIZE] = {};

    curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, errorText);
    curl_easy_setopt (curl, CURLOPT_URL, streamUrl.c_str ());
    curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt (curl, CURLOPT_HTTP200ALIASES, aliases);
    curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt (curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, headerTrampoline);
    curl_easy_setopt (curl, CURLOPT_HEADERDATA, this);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, bodyTrampoline);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, this);
    curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L);

    // Stopped, it does not wait for a name lookup that is not answering.
    curl_easy_setopt (curl, CURLOPT_QUICK_EXIT, 1L);

    // A live stream never ends on its own, so a stalled one has to be given up on.
    curl_easy_setopt (curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt (curl, CURLOPT_LOW_SPEED_TIME, STALL_SECONDS);

    if (timeoutSeconds > 0)
    {
        curl_easy_setopt (curl, CURLOPT_TIMEOUT, timeoutSeconds);
    }

    if (m_shouldAbort)
    {
        curl_easy_setopt (curl, CURLOPT_XFERINFODATA, this);
        curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION,
            +[] (void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int
            {
                IcyReader *self = static_cast<IcyReader *> (clientp);

                if (self->m_shouldAbort ())
                {
                    self->m_stopped = true;
                    return (1);
                }

                return (0);
            });
        curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);
    }

    const CURLcode result = curl_easy_perform (curl);

    curl_off_t elapsedMicroseconds = 0;

    curl_easy_getinfo (curl, CURLINFO_TOTAL_TIME_T, &elapsedMicroseconds);

    curl_easy_cleanup (curl);
    curl_slist_free_all (headers);
    curl_slist_free_all (aliases);

    // Aborting from a callback is how we stop, so that is a success, not a failure.
    if (m_stopped)
    {
        return (true);
    }

    // Reaching the caller's cap is expected when nothing was found in the window. The same error
    // code also covers a connection that never opened and a station that went quiet for the
    // low-speed limit, which are real failures; libcurl's text tells them apart, such as
    // "Connection timed out after 5001 milliseconds" or "Operation too slow".
    if (result == CURLE_OPERATION_TIMEDOUT)
    {
        // Allow 100ms for libcurl's timer granularity, so a cap that fired is never taken for a stall.
        const curl_off_t capMicroseconds = static_cast<curl_off_t> (timeoutSeconds) * 1000000 - 100000;

        if (timeoutSeconds > 0 && elapsedMicroseconds >= capMicroseconds)
        {
            return (m_error.empty ());
        }

        m_error = (errorText[0] != '\0') ? std::string (errorText) : curl_easy_strerror (result);
        return (false);
    }

    if (result != CURLE_OK && result != CURLE_WRITE_ERROR)
    {
        m_error = (errorText[0] != '\0') ? std::string (errorText) : curl_easy_strerror (result);
        return (false);
    }

    return (m_error.empty ());
}
