#include "StreamProxy.h"
#include "IcyDemuxer.h"
#include "Say.h"
#include <curl/curl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <system_error>

// Writes all of it, or reports that the peer is gone.
static bool sendAll (int fd, const char *data, size_t length)
{
    while (length > 0)
    {
        const ssize_t sent = send (fd, data, length, MSG_NOSIGNAL);

        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return (false);
        }

        data += sent;
        length -= static_cast<size_t> (sent);
    }

    return (true);
}

// The speaker sends nothing after its request, so a connection that turns readable has been
// hung up on.
static bool peerHungUp (int fd)
{
    struct pollfd pfd { fd, static_cast<short> (POLLIN | POLLRDHUP), 0 };

    if (poll (&pfd, 1, 0) <= 0)
    {
        return (false);
    }

    if ((pfd.revents & (POLLRDHUP | POLLHUP | POLLERR)) != 0)
    {
        return (true);
    }

    char probe = 0;

    return (recv (fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT) == 0);
}

static std::string percentEncode (const std::string &text)
{
    static const char hex[] = "0123456789ABCDEF";

    std::string encoded;

    for (const unsigned char c : text)
    {
        if (std::isalnum (c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded += static_cast<char> (c);
        }
        else
        {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 0x0F];
        }
    }

    return (encoded);
}

static std::string percentDecode (const std::string &text)
{
    std::string decoded;

    for (size_t i = 0; i < text.size (); ++i)
    {
        if (text[i] == '%' && i + 2 < text.size ()
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

StreamProxy::StreamProxy ()
    : m_ring (),
      m_ringBytes (0),
      m_written (STREAM_BASE),
      m_generationStart (STREAM_BASE),
      m_generation (0),
      m_headersGeneration (0),
      m_metadataOffered (false),
      m_metaInterval (0),
      m_bitrateKbps (0),
      m_measuredRate (0),
      m_rateBaseSet (false),
      m_rateBaseBytes (0),
      m_connectionStart (0),
      m_speakerClient (nullptr),
      m_latestSpeakerSequence (0),
      m_speakerGeneration (0),
      m_speakerStart (0),
      m_speakerSent (0),
      m_speakerStopped (false),
      m_speakerStoppedAt (0),
      m_speakerOrigin (0),
      m_speakerOriginGeneration (0),
      m_speakerLeftAt (0),
      m_speakerLeftGeneration (0),
      m_resumePending (false),
      m_resumeGeneration (0),
      m_resumeOffset (0),
      m_intendedPending (false),
      m_intendedAt (0),
      m_intendedGeneration (0),
      m_speakerConnections (0),
      m_titleOffsetSeconds (0),
      m_resumedWhileGone (false),
      m_restartPending (false),
      m_restartGeneration (0),
      m_port (0),
      m_listenFd (-1),
      m_running (false),
      m_stopping (false),
      m_acceptSequence (0)
{
}

StreamProxy::~StreamProxy ()
{
    stop ();
}

std::uint64_t StreamProxy::oldestLocked () const
{
    const std::uint64_t ringFloor = (m_written > m_ringBytes) ? (m_written - m_ringBytes) : 0;

    return (std::max (ringFloor, m_generationStart));
}

// The oldest byte a speaker may be started on. Older ones are about to be overwritten while it
// reads, so the oldest RESUME_MARGIN of the history is left alone.
std::uint64_t StreamProxy::resumeFloorLocked () const
{
    const std::uint64_t usable = m_ringBytes - RESUME_MARGIN;
    const std::uint64_t ringFloor = (m_written > usable) ? (m_written - usable) : 0;

    return (std::max (ringFloor, m_generationStart));
}

// The title playing at a stream byte: the newest one that took effect at or before it. The audio
// before a station's first metadata block belongs to the song that block names, if it named one;
// empty titles are dropped, so a first title found later is a song still to come.
StreamProxy::TitleMark StreamProxy::titleAtLocked (std::uint64_t position) const
{
    const bool fromFirstBlock = !m_titles.empty ()
        && m_titles.front ().offset <= m_generationStart + static_cast<std::uint64_t> (m_metaInterval);

    TitleMark mark = fromFirstBlock ? m_titles.front () : TitleMark { 0, std::string () };

    for (const TitleMark &candidate : m_titles)
    {
        if (candidate.offset > position)
        {
            break;
        }

        mark = candidate;
    }

    return (mark);
}

// Where a speaker that has fallen further behind than the relay holds starts again: the song now
// playing, from its beginning, so it never lands part way into one. That is the newest title that
// began inside the history and has been on for CATCH_UP_LEAD_SECONDS, which also rules out a
// station ID that the next title soon replaced. Without one, as on a station that sends no
// titles, it starts on the usual backlog.
// Where the song a title names really begins: the title change moved by the title offset, but never
// back to or before the title before it, on past the next one, or past what has arrived.
std::uint64_t StreamProxy::songStartLocked (size_t index) const
{
    const TitleMark &mark = m_titles[index];
    const long rate = bytesPerSecondLocked ();

    if (rate <= 0 || m_titleOffsetSeconds == 0)
    {
        return (mark.offset);
    }

    const double shifted = static_cast<double> (mark.offset) + m_titleOffsetSeconds * static_cast<double> (rate);
    const std::uint64_t lower = (index > 0) ? m_titles[index - 1].offset + 1 : m_generationStart;
    const std::uint64_t upper = std::min (m_written, (index + 1 < m_titles.size ()) ? m_titles[index + 1].offset - 1 : m_written);

    if (lower > upper)
    {
        return (mark.offset);
    }

    if (shifted <= static_cast<double> (lower))
    {
        return (lower);
    }

    return (std::min (upper, static_cast<std::uint64_t> (std::llround (shifted))));
}

StreamProxy::CatchUp StreamProxy::catchUpLocked () const
{
    const long rate = bytesPerSecondLocked ();

    if (rate > 0)
    {
        const std::uint64_t floor = resumeFloorLocked ();
        const std::uint64_t lead = static_cast<std::uint64_t> (rate) * CATCH_UP_LEAD_SECONDS;
        const std::uint64_t firstBlockEnd = m_generationStart + static_cast<std::uint64_t> (m_metaInterval);
        std::uint64_t nextStart = std::numeric_limits<std::uint64_t>::max ();

        for (size_t index = m_titles.size (); index-- > 0; )
        {
            const TitleMark &mark = m_titles[index];

            if (mark.offset < floor)
            {
                break;
            }

            // The first block's title names a song already under way when the station was joined,
            // not one starting there. The lead counts from where it would start.
            const std::uint64_t begins = std::max (songStartLocked (index), floor);

            if (mark.offset > firstBlockEnd && m_written - begins >= lead && nextStart - mark.offset >= lead)
            {
                return (CatchUp { begins, mark });
            }

            nextStart = mark.offset;
        }
    }

    const std::uint64_t start = backlogStartLocked ();

    return (CatchUp { start, titleAtLocked (start) });
}

std::uint64_t StreamProxy::backlogStartLocked () const
{
    const std::uint64_t burstFloor = (m_written > BURST_BYTES) ? (m_written - BURST_BYTES) : 0;

    return (std::max (burstFloor, m_generationStart));
}

// Stations round their nominal bitrate inconsistently: this CDN says 63 or 64 at random for the
// same 64 kbps stream. The playback estimate multiplies the rate by minutes, so 1.6% low means
// pushing ~3s late after a song, and the resume then replays those 3s. Snap to the standard rate
// meant, when one is within 5%.
long StreamProxy::standardBitrate (long kbps)
{
    static const long rates[] = { 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 };

    if (kbps <= 0)
    {
        return (0);
    }

    long nearest = rates[0];

    for (const long rate : rates)
    {
        if (std::labs (rate - kbps) < std::labs (nearest - kbps))
        {
            nearest = rate;
        }
    }

    return ((std::labs (nearest - kbps) * 20 <= nearest) ? nearest : kbps);
}

long StreamProxy::bytesPerSecondLocked () const
{
    if (m_bitrateKbps > 0)
    {
        return (m_bitrateKbps * 125);
    }

    return (m_measuredRate);
}

// How far the audio received on this connection has got ahead of real time. It grows while the
// station sends its opening backlog and then holds steady.
double StreamProxy::burstExcessLocked (long bytesPerSecond) const
{
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now () - m_connectedAt;

    return (static_cast<double> (m_written - m_connectionStart) - elapsed.count () * static_cast<double> (bytesPerSecond));
}

// Where the speaker is, by the clock: from where its connection started, at the stream's rate.
// It cannot have played what it has not been sent, so the estimate never passes that.
std::uint64_t StreamProxy::speakerEstimateLocked (std::chrono::steady_clock::time_point now) const
{
    if (m_speakerStopped)
    {
        return (std::min (m_speakerStoppedAt, m_speakerSent));
    }

    const long rate = bytesPerSecondLocked ();
    std::uint64_t position = m_speakerStart;

    if (rate > 0)
    {
        const long long playingMs = std::chrono::duration_cast<std::chrono::milliseconds> (
            now - m_speakerSince - SPEAKER_STARTUP).count ();

        if (playingMs > 0)
        {
            position += static_cast<std::uint64_t> (playingMs) * static_cast<std::uint64_t> (rate) / 1000;
        }
    }
    else
    {
        position = m_speakerSent;       // no rate, no estimate: take what was sent as heard
    }

    return (std::min (position, m_speakerSent));
}

// A speaker that has played everything it was sent is waiting, not playing, so its clock is held
// back by however long that lasts. Otherwise the estimate would run ahead of it through an outage.
void StreamProxy::holdSpeakerClockLocked (std::chrono::steady_clock::time_point now)
{
    const long rate = bytesPerSecondLocked ();

    if (rate <= 0 || m_speakerStopped)
    {
        return;
    }

    const long long playingMs = std::chrono::duration_cast<std::chrono::milliseconds> (
        now - m_speakerSince - SPEAKER_STARTUP).count ();

    if (playingMs <= 0)
    {
        return;
    }

    const std::uint64_t byClock = m_speakerStart
                                + static_cast<std::uint64_t> (playingMs) * static_cast<std::uint64_t> (rate) / 1000;

    if (byClock > m_speakerSent)
    {
        m_speakerSince += std::chrono::milliseconds (
            static_cast<long long> ((byClock - m_speakerSent) * 1000 / static_cast<std::uint64_t> (rate)));
    }
}

void StreamProxy::copyOut (std::uint64_t position, size_t length, char *destination) const
{
    size_t done = 0;

    while (done < length)
    {
        const size_t slot = static_cast<size_t> ((position + done) % m_ringBytes);
        const size_t run = std::min (length - done, m_ringBytes - slot);

        std::memcpy (destination + done, m_ring.get () + slot, run);
        done += run;
    }
}

void StreamProxy::append (std::uint64_t generation, const char *data, size_t length)
{
    {
        std::lock_guard<std::mutex> lock (m_mutex);

        // A transfer still draining after a station change must not leak into the new one.
        if (generation != m_generation)
        {
            return;
        }

        size_t done = 0;

        while (done < length)
        {
            const size_t slot = static_cast<size_t> ((m_written + done) % m_ringBytes);
            const size_t run = std::min (length - done, m_ringBytes - slot);

            std::memcpy (m_ring.get () + slot, data + done, run);
            done += run;
        }

        m_written += length;

        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ();

        if (!m_rateBaseSet && now - m_connectedAt >= RATE_SETTLE)
        {
            m_rateBaseSet = true;
            m_rateBaseBytes = m_written;
            m_rateBaseTime = now;
        }

        // Kept across reconnects to the same station, so a drop does not lose the estimate.
        if (m_rateBaseSet)
        {
            const std::chrono::duration<double> elapsed = now - m_rateBaseTime;

            if (elapsed >= RATE_WINDOW)
            {
                m_measuredRate = static_cast<long> (static_cast<double> (m_written - m_rateBaseBytes)
                                                    / elapsed.count ());
            }
        }
    }

    m_changed.notify_all ();
}

void StreamProxy::addTitle (std::uint64_t generation, const std::string &title)
{
    {
        std::lock_guard<std::mutex> lock (m_mutex);

        if (generation != m_generation)
        {
            return;
        }

        if (!m_titles.empty () && m_titles.back ().title == title)
        {
            return;
        }

        m_titles.push_back (TitleMark { m_written, title });

        // Keep the newest title that has aged out of the history: it is still the one playing
        // at the oldest byte held.
        const std::uint64_t oldest = oldestLocked ();

        while (m_titles.size () > 1 && m_titles[1].offset <= oldest)
        {
            m_titles.pop_front ();
        }
    }

    m_changed.notify_all ();
}

void StreamProxy::publishHeaders (std::uint64_t generation, const IcyDemuxer &demuxer)
{
    {
        std::lock_guard<std::mutex> lock (m_mutex);

        if (generation != m_generation)
        {
            return;
        }

        // A reconnect that leaves these out keeps what the station said before.
        if (!demuxer.getContentType ().empty ())
        {
            m_contentType = demuxer.getContentType ();
        }

        const long kbps = standardBitrate (demuxer.getBitrateKbps ());

        if (kbps > 0)
        {
            m_bitrateKbps = kbps;
        }

        m_headersGeneration = generation;
        m_metadataOffered = (demuxer.getMetaInterval () > 0);
        m_metaInterval = demuxer.getMetaInterval ();
        m_connectedAt = std::chrono::steady_clock::now ();
        m_connectionStart = m_written;
        m_rateBaseSet = false;
    }

    m_changed.notify_all ();
}

void StreamProxy::pumpAudio (PumpContext &context, const char *data, size_t length)
{
    if (!context.splicing)
    {
        append (context.generation, data, length);
        return;
    }

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ();

    if (context.held.empty ())
    {
        context.holdStart = now;
        context.holdGrew = now;
        context.holdExcess = 0;
    }

    // Only the new bytes, plus enough before them to catch a match straddling the boundary.
    const size_t keyLength = context.spliceKey.size ();
    const size_t searchFrom = (context.held.size () >= keyLength) ? (context.held.size () - keyLength + 1) : 0;

    context.held.insert (context.held.end (), data, data + length);

    const std::vector<char>::iterator found = std::search (
        context.held.begin () + static_cast<std::ptrdiff_t> (searchFrom), context.held.end (),
        std::boyer_moore_horspool_searcher (context.spliceKey.begin (), context.spliceKey.end ()));

    if (found != context.held.end ())
    {
        const size_t from = static_cast<size_t> (found - context.held.begin ()) + keyLength;

        Say () << ">>> relay: reconnected; skipped " << from << " bytes the station sent again\n";

        finishSplice (context, from);

        return;
    }

    // A repeat can only lie in the backlog the station opens with, the part arriving ahead of
    // real time. Once the audio has kept to the stream's own pace for SPLICE_SETTLE with no
    // match, that backlog is over and the drop lost audio rather than repeating it. Without a
    // rate, allow as long as any burst takes.
    long rate = 0;

    {
        std::lock_guard<std::mutex> lock (m_mutex);

        rate = bytesPerSecondLocked ();
    }

    const std::chrono::duration<double> holding = now - context.holdStart;
    const double excess = static_cast<double> (context.held.size ()) - holding.count () * static_cast<double> (rate);

    if (excess > context.holdExcess + BURST_SLACK)
    {
        context.holdExcess = excess;
        context.holdGrew = now;
    }

    const bool backlogOver = (rate > 0) ? (now - context.holdGrew >= SPLICE_SETTLE)
                                        : (now - context.holdStart >= BURST_LIMIT);

    if (backlogOver || context.held.size () >= SPLICE_HOLD_LIMIT)
    {
        Say () << ">>> relay: reconnected; nothing repeated, so some audio was lost\n";

        finishSplice (context, 0);
    }
}

void StreamProxy::pumpTitle (PumpContext &context, const std::string &title)
{
    if (context.splicing)
    {
        context.heldTitles.push_back (HeldTitle { context.held.size (), title });
        return;
    }

    addTitle (context.generation, title);
}

// Releases the held audio from `from` on, with the titles in it at their places.
void StreamProxy::finishSplice (PumpContext &context, size_t from)
{
    // Titles inside the repeated audio were mostly recorded the first time round. Not always:
    // a song that changed just before the drop may not have reached the old connection's next
    // metadata block. So the newest of them is still the one in force where the new audio
    // begins; addTitle ignores it if it is already the latest.
    const HeldTitle *inForce = nullptr;

    for (const HeldTitle &held : context.heldTitles)
    {
        if (held.at < from)
        {
            inForce = &held;
        }
    }

    if (inForce != nullptr)
    {
        addTitle (context.generation, inForce->title);
    }

    size_t at = from;

    for (const HeldTitle &held : context.heldTitles)
    {
        if (held.at < from)
        {
            continue;
        }

        append (context.generation, context.held.data () + at, held.at - at);
        at = held.at;

        addTitle (context.generation, held.title);
    }

    append (context.generation, context.held.data () + at, context.held.size () - at);

    context.splicing = false;
    context.held.clear ();
    context.held.shrink_to_fit ();
    context.heldTitles.clear ();

    // The audio just released arrived over time but was appended at once; leave it out of the
    // measured rate, as the opening burst is.
    {
        std::lock_guard<std::mutex> lock (m_mutex);

        m_rateBaseSet = false;
        m_connectedAt = std::chrono::steady_clock::now ();
    }
}

size_t StreamProxy::curlHeader (char *buffer, size_t size, size_t items, void *userp)
{
    PumpContext *context = static_cast<PumpContext *> (userp);

    context->demuxer->onHeader (std::string (buffer, size * items));

    return (size * items);
}

size_t StreamProxy::curlWrite (char *buffer, size_t size, size_t items, void *userp)
{
    PumpContext *context = static_cast<PumpContext *> (userp);
    const size_t total = size * items;

    // The first body byte means the final response's headers are all in.
    if (!context->published)
    {
        context->published = true;
        context->self->publishHeaders (context->generation, *context->demuxer);
    }

    context->demuxer->onBody (buffer, total);

    return (total);
}

void StreamProxy::pumpLoop ()
{
    long backoffSeconds = 0;

    while (!m_stopping)
    {
        std::string url;
        PumpContext context { this, nullptr, 0, false, false, {}, {}, {}, {}, {}, 0 };

        {
            std::unique_lock<std::mutex> lock (m_mutex);

            m_changed.wait_for (lock, std::chrono::milliseconds (200),
                                [this] { return (!m_upstream.empty () || m_stopping); });

            url = m_upstream;
            context.generation = m_generation;

            // Back on the same station after a drop. It will open with a backlog the speaker has
            // already been sent, so remember how the audio ended to find where the repeat stops.
            if (m_written >= m_generationStart + SPLICE_KEY_BYTES)
            {
                context.splicing = true;
                context.spliceKey.resize (SPLICE_KEY_BYTES);

                copyOut (m_written - SPLICE_KEY_BYTES, SPLICE_KEY_BYTES, context.spliceKey.data ());
            }
        }

        const std::uint64_t generation = context.generation;

        if (m_stopping)
        {
            break;
        }

        if (url.empty ())
        {
            continue;
        }

        IcyDemuxer demuxer (
            [this, &context] (const char *data, size_t length)
            {
                pumpAudio (context, data, length);
            },
            [this, &context] (const std::string &title)
            {
                pumpTitle (context, title);
                return (true);
            });

        context.demuxer = &demuxer;

        CURL *curl = curl_easy_init ();

        if (curl == nullptr)
        {
            std::this_thread::sleep_for (std::chrono::seconds (1));
            continue;
        }

        // Ask for the titles: the relay strips them out, so the speaker still gets plain audio.
        struct curl_slist *headers = curl_slist_append (nullptr, "Icy-MetaData: 1");

        // Shoutcast v1 servers answer "ICY 200 OK", which libcurl otherwise rejects as HTTP/0.9.
        struct curl_slist *aliases = curl_slist_append (nullptr, "ICY 200 OK");

        curl_easy_setopt (curl, CURLOPT_URL, url.c_str ());
        curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt (curl, CURLOPT_HTTP200ALIASES, aliases);
        curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt (curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, curlHeader);
        curl_easy_setopt (curl, CURLOPT_HEADERDATA, &context);
        curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, curlWrite);
        curl_easy_setopt (curl, CURLOPT_WRITEDATA, &context);
        curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L);

        // An aborted transfer does not wait for a name lookup that is not answering, which would
        // hold up a station change or a stop for the resolver's whole timeout.
        curl_easy_setopt (curl, CURLOPT_QUICK_EXIT, 1L);
        curl_easy_setopt (curl, CURLOPT_USERAGENT, "soundtouch-proxy");

        // A live stream never ends on its own, so a stalled one has to be given up on.
        curl_easy_setopt (curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt (curl, CURLOPT_LOW_SPEED_TIME, 15L);

        // Abort the transfer when the upstream is re-pointed or we are shutting down.
        curl_easy_setopt (curl, CURLOPT_XFERINFODATA, &context);
        curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION,
            +[] (void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int
            {
                const PumpContext *pump = static_cast<const PumpContext *> (clientp);
                const StreamProxy *self = pump->self;

                return ((self->m_stopping || self->m_generation != pump->generation) ? 1 : 0);
            });
        curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);

        const CURLcode result = curl_easy_perform (curl);

        curl_easy_cleanup (curl);
        curl_slist_free_all (headers);
        curl_slist_free_all (aliases);

        if (m_stopping)
        {
            break;
        }

        // Dropped again before it caught up. What is held cannot be placed, so let it go; the
        // next connection matches against the same ending.
        if (context.splicing && !context.held.empty () && m_generation == generation)
        {
            Say () << ">>> relay: connection dropped before it caught up; retrying\n";
        }

        if (m_generation != generation)
        {
            backoffSeconds = 0;
            continue;                   // re-pointed: go straight to the new station
        }

        // The same station dropped us or never answered. Retry after a second following a working
        // connection, backing off only while it keeps failing.
        backoffSeconds = context.published ? 1 : std::min (backoffSeconds + 2, 30L);

        Say () << ">>> relay: upstream " << (context.published ? "ended" : "failed")
               << " (" << curl_easy_strerror (result) << "); reconnecting in "
               << backoffSeconds << "s\n";

        std::unique_lock<std::mutex> lock (m_mutex);

        m_changed.wait_for (lock, std::chrono::seconds (backoffSeconds),
                            [this, generation] { return (m_stopping || m_generation != generation); });
    }
}

void StreamProxy::serveClient (Client *client)
{
    const int fd = client->fd;

    // Read the request head. The speaker sends a bare GET.
    std::string request;
    char scratch[2048];

    while (request.find ("\r\n\r\n") == std::string::npos && request.size () < 8192 && !m_stopping)
    {
        struct pollfd pfd { fd, POLLIN, 0 };

        if (poll (&pfd, 1, 5000) <= 0)
        {
            break;
        }

        const ssize_t got = recv (fd, scratch, sizeof (scratch), 0);

        if (got <= 0)
        {
            break;
        }

        request.append (scratch, static_cast<size_t> (got));
    }

    const bool isGet = (request.compare (0, 4, "GET ") == 0);
    const bool isHead = (request.compare (0, 5, "HEAD ") == 0);
    const bool isSpeaker = (isGet && client->fromSpeaker);

    // Resuming from a pause, the speaker asks to carry on from byte N of the stream it had.
    std::uint64_t rangeStart = 0;
    {
        std::string lower = request;

        std::transform (lower.begin (), lower.end (), lower.begin (),
                        [] (unsigned char c) { return (static_cast<char> (std::tolower (c))); });

        static const std::string key = "\r\nrange: bytes=";
        const size_t at = lower.find (key);

        if (at != std::string::npos)
        {
            rangeStart = std::strtoull (lower.c_str () + at + key.size (), nullptr, 10);
        }
    }

    const bool ranged = (isSpeaker && rangeStart > 0);

    // What the speaker asks for, headers and all: how it resumes after a pause shows up here.
    if (client->fromSpeaker)
    {
        const size_t headEnd = request.find ("\r\n\r\n");
        std::string head = request.substr (0, headEnd == std::string::npos ? request.size () : headEnd);

        for (size_t at = head.find ("\r\n"); at != std::string::npos; at = head.find ("\r\n", at))
        {
            head.replace (at, 2, " | ");
        }

        Say () << ">>> relay: speaker asked: " << head << "\n";
    }

    // The newest of the speaker's connections is the live one, whatever order they get going in.
    if (isSpeaker)
    {
        std::lock_guard<std::mutex> lock (m_mutex);

        m_latestSpeakerSequence = std::max (m_latestSpeakerSequence, client->sequence);
    }

    std::uint64_t generation = 0;
    std::uint64_t position = 0;
    std::uint64_t claimedAt = 0;
    std::string contentType = "audio/aacp";

    // For the log line about each speaker connection, the one place a dropped stream shows up.
    bool claimed = false;
    bool resumed = false;
    bool pausePoint = false;
    bool unheld = false;            // it asked to carry on from bytes no longer held
    double behindSeconds = -1;
    const char *ended = "the speaker hung up";
    bool serve = (isGet || isHead);

    if (serve)
    {
        std::unique_lock<std::mutex> lock (m_mutex);

        // Pass on the station's own Content-Type, which needs its response headers first.
        m_changed.wait_for (lock, std::chrono::seconds (5), [this]
        {
            return (m_stopping || (m_generation != 0 && m_headersGeneration == m_generation));
        });

        generation = m_generation;
        position = backlogStartLocked ();
        serve = !m_stopping;

        if (m_headersGeneration == generation && !m_contentType.empty ())
        {
            contentType = m_contentType;
        }

        if (serve && isSpeaker && client->sequence != m_latestSpeakerSequence)
        {
            serve = false;              // a newer connection from the speaker takes over
        }
        else if (serve && isSpeaker && ranged)
        {
            // Back from a pause it asks for byte N of the stream it had, and checks that what
            // comes is that. Only the bytes it had next will do: a pause point that is no longer
            // held, or one from before control was restarted, has it started again instead.
            if (m_speakerOriginGeneration == generation)
            {
                const std::int64_t wanted = m_speakerOrigin + static_cast<std::int64_t> (rangeStart);

                if (wanted >= static_cast<std::int64_t> (resumeFloorLocked ()) && wanted <= static_cast<std::int64_t> (m_written))
                {
                    position = static_cast<std::uint64_t> (wanted);
                    pausePoint = true;
                }
            }

            if (!pausePoint && generation != 0)
            {
                unheld = true;
                serve = false;
                m_restartPending = true;
                m_restartGeneration = generation;
            }
        }

        if (serve && isSpeaker)
        {
            if (!pausePoint && m_resumePending && m_resumeGeneration == generation)
            {
                resumed = true;

                // Carry on from where the re-issue interrupted it, as long as that is still held.
                position = std::min (std::max ({ m_resumeOffset, resumeFloorLocked () }), m_written);
            }

            // Back from a pause it asks for the bytes after those it already holds, but plays on
            // from where it paused, further back by whatever it had buffered. Count its position
            // from there, or the next song is pushed that much too early. Its old connection may not
            // have been seen to end yet: then where that one had got to (held, if paused) is it.
            std::uint64_t playingFrom = position;
            std::uint64_t pausedAt = 0;

            if (pausePoint && m_speakerClient != nullptr && m_speakerGeneration == generation
                && bytesPerSecondLocked () > 0)
            {
                pausedAt = speakerEstimateLocked (std::chrono::steady_clock::now ());
            }
            else if (pausePoint && m_speakerLeftGeneration == generation)
            {
                pausedAt = m_speakerLeftAt;
            }

            if (pausedAt != 0 && pausedAt <= position)
            {
                playingFrom = pausedAt;

                // Played again before it reconnected: it has been playing its own buffer since.
                const long rate = bytesPerSecondLocked ();

                if (m_resumedWhileGone && m_speakerClient == nullptr && rate > 0)
                {
                    const long long playedMs = std::chrono::duration_cast<std::chrono::milliseconds> (
                        std::chrono::steady_clock::now () - m_resumedWhileGoneTime - SPEAKER_STARTUP).count ();

                    if (playedMs > 0)
                    {
                        playingFrom = std::min (position, pausedAt + static_cast<std::uint64_t> (playedMs)
                                                          * static_cast<std::uint64_t> (rate) / 1000);
                    }
                }
            }

            m_resumePending = false;
            m_restartPending = false;
            m_intendedPending = false;
            m_resumedWhileGone = false;
            m_speakerClient = client;
            m_speakerGeneration = generation;
            m_speakerStart = playingFrom;
            m_speakerSent = position;
            m_speakerSince = std::chrono::steady_clock::now ();
            m_speakerStopped = false;
            ++m_speakerConnections;
            m_speakerConnectedAt = m_speakerSince;

            // Byte N of what this connection sends is position + (N - rangeStart): keep the
            // speaker's own numbering, so its next pause resumes correctly too.
            m_speakerOrigin = static_cast<std::int64_t> (position) - static_cast<std::int64_t> (rangeStart);
            m_speakerOriginGeneration = generation;

            // Nothing played yet on this connection: if it ends at once, it left off where it began.
            claimedAt = (bytesPerSecondLocked () > 0) ? playingFrom : 0;

            claimed = true;

            if (bytesPerSecondLocked () > 0)
            {
                behindSeconds = static_cast<double> (m_written - position) / static_cast<double> (bytesPerSecondLocked ());
            }
        }
    }

    // A speaker connection it replaces is waiting on this to notice and step aside, and control
    // on a speaker waiting to be started again.
    m_changed.notify_all ();

    // Each line goes out in one write, so other threads' output cannot land in the middle of it.
    if (claimed)
    {
        Say line;

        line << ">>> relay: speaker connected";

        if (behindSeconds >= 0)
        {
            line << ", " << tenths (behindSeconds) << "s behind the station";
        }

        line << (pausePoint ? " (from where it paused)"
                 : resumed ? " (from a set point)"
                 : " (from the backlog)") << "\n";
    }

    // Left unanswered until control starts it again, which makes it open a new connection. Sent
    // anything else, it would only turn it down and ask again. It may still be paused, its open
    // connection having been dropped: then it waits here, for however long the pause lasts.
    if (unheld)
    {
        Say () << ">>> relay: the speaker asked to carry on from where it paused, which is no longer "
               << "held; it has to be started again\n";

        std::unique_lock<std::mutex> lock (m_mutex);

        while (!m_stopping && m_generation == generation && client->sequence == m_latestSpeakerSequence
               && !peerHungUp (fd))
        {
            m_changed.wait_for (lock, HANGUP_CHECK);
        }
    }

    const std::chrono::steady_clock::time_point connectedAt = std::chrono::steady_clock::now ();
    const std::uint64_t firstByte = position;

    if (serve)
    {
        // The speaker turns down a plain 200 when it asked for a range and just asks again, and
        // turns down an open-ended range too. So present the stream as a very large file: a
        // numeric total, a Content-Length, and a range that runs to its end.
        const std::uint64_t pretendTotal = rangeStart + 0x7FFFFFFFULL;
        const std::string header = ranged
            ? "HTTP/1.1 206 Partial Content\r\n"
              "Content-Type: " + contentType + "\r\n"
              "Accept-Ranges: bytes\r\n"
              "Content-Length: " + std::to_string (pretendTotal - rangeStart) + "\r\n"
              "Content-Range: bytes " + std::to_string (rangeStart) + "-"
                  + std::to_string (pretendTotal - 1) + "/" + std::to_string (pretendTotal) + "\r\n"
              "Connection: close\r\n"
              "\r\n"
            : "HTTP/1.0 200 OK\r\n"
              "Content-Type: " + contentType + "\r\n"
              "Connection: close\r\n"
              "\r\n";

        serve = sendAll (fd, header.data (), header.size ()) && isGet;
    }

    std::vector<char> outgoing (SEND_CHUNK);

    // Where the speaker had got to when last seen connected, which is where it left off if it
    // hangs up; 0 while the rate is unknown. It is checked often, since a hang-up only shows
    // otherwise on a failed send.
    std::uint64_t lastSeenAt = claimedAt;
    const std::chrono::milliseconds wake = isSpeaker ? HANGUP_CHECK : std::chrono::milliseconds (500);

    while (serve && !m_stopping)
    {
        size_t pending = 0;

        {
            std::unique_lock<std::mutex> lock (m_mutex);

            m_changed.wait_for (lock, wake, [&]
            {
                return (m_stopping || m_generation != generation
                        || (isSpeaker && m_speakerClient != client) || m_written > position);
            });

            if (m_stopping || m_generation != generation)
            {
                ended = m_stopping ? "the relay is stopping" : "the station changed";
                break;                  // the next station belongs to a new connection
            }

            if (isSpeaker && m_speakerClient != client)
            {
                ended = "the speaker opened a newer one";
                break;                  // the speaker has opened a newer connection
            }

            if (isSpeaker)
            {
                const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ();

                if (peerHungUp (fd))
                {
                    break;
                }

                holdSpeakerClockLocked (now);
                lastSeenAt = (bytesPerSecondLocked () > 0) ? speakerEstimateLocked (now) : 0;
            }

            if (m_written <= position)
            {
                continue;
            }

            // Stalled for longer than the history holds: those bytes are gone. Anyone else jumps
            // to the backlog. The speaker would only turn down bytes that do not follow on, so
            // let it go; asking to carry on later, it is started again.
            if (m_written - position > m_ringBytes)
            {
                if (isSpeaker)
                {
                    ended = "it stopped reading for longer than the relay holds";
                    break;
                }

                position = backlogStartLocked ();
            }

            pending = static_cast<size_t> (std::min<std::uint64_t> (m_written - position, SEND_CHUNK));

            copyOut (position, pending, outgoing.data ());
            position += pending;
        }

        serve = sendAll (fd, outgoing.data (), pending);

        if (!serve)
        {
            ended = "sending failed";
        }

        if (serve && isSpeaker)
        {
            std::lock_guard<std::mutex> lock (m_mutex);

            if (m_speakerClient == client)
            {
                m_speakerSent = position;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock (m_mutex);

        if (m_speakerClient == client)
        {
            // A speaker a press had stopped left off at the press, which is more exact than the last
            // look; one that hung up by itself left off at that last look.
            m_speakerLeftAt = (m_speakerStopped && m_speakerStoppedAt != 0)
                            ? speakerEstimateLocked (std::chrono::steady_clock::now ()) : lastSeenAt;
            m_speakerLeftGeneration = m_speakerGeneration;
            m_speakerLeftTime = m_speakerStopped ? m_speakerStoppedTime
                                                 : std::chrono::steady_clock::now ();
            m_speakerClient = nullptr;
        }
    }

    if (claimed)
    {
        const std::chrono::duration<double> lasted = std::chrono::steady_clock::now () - connectedAt;

        Say () << ">>> relay: speaker connection ended after " << tenths (lasted.count ())
               << "s, " << (position - firstByte) / 1024 << " KB sent: " << ended << "\n";
    }

    // The socket is closed by whoever joins this thread, so stop() can still shut it down.
    client->done = true;
}

void StreamProxy::reapClients (bool all)
{
    if (all)
    {
        // Unblocks any send or recv in progress so the threads can be joined.
        for (const std::unique_ptr<Client> &client : m_clients)
        {
            shutdown (client->fd, SHUT_RDWR);
        }
    }

    for (auto client = m_clients.begin (); client != m_clients.end (); )
    {
        if (all || (*client)->done)
        {
            if ((*client)->thread.joinable ())
            {
                (*client)->thread.join ();
            }

            close ((*client)->fd);
            client = m_clients.erase (client);
        }
        else
        {
            ++client;
        }
    }
}

void StreamProxy::acceptLoop ()
{
    while (!m_stopping)
    {
        reapClients (false);

        struct pollfd pfd { m_listenFd, POLLIN, 0 };

        if (poll (&pfd, 1, 300) <= 0)
        {
            continue;
        }

        struct sockaddr_in peer;
        socklen_t peerLength = sizeof (peer);

        const int fd = accept (m_listenFd, reinterpret_cast<struct sockaddr *> (&peer), &peerLength);

        if (fd < 0)
        {
            continue;
        }

        bool fromSpeaker = false;
        char text[INET_ADDRSTRLEN] = {};

        if (peer.sin_family == AF_INET && inet_ntop (AF_INET, &peer.sin_addr, text, sizeof (text)) != nullptr)
        {
            fromSpeaker = (m_speakerIp == text);
        }

        if (!fromSpeaker && m_clients.size () >= MAX_CLIENTS)
        {
            close (fd);
            continue;
        }

        const int one = 1;

        setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof (one));

        // A peer that stops reading without closing would otherwise hold a thread forever.
        struct timeval sendTimeout { 30, 0 };

        setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof (sendTimeout));

        std::unique_ptr<Client> client = std::make_unique<Client> (fd, ++m_acceptSequence, fromSpeaker);

        try
        {
            client->thread = std::thread (&StreamProxy::serveClient, this, client.get ());
        }
        catch (const std::system_error &error)
        {
            Say (std::cerr) << "proxy: could not serve a connection: " << error.what () << "\n";
            close (fd);
            continue;
        }

        m_clients.push_back (std::move (client));
    }
}

bool StreamProxy::start (int port, const std::string &speakerIp, size_t bufferBytes)
{
    // Left uninitialised, so the memory is only taken up as the stream fills it.
    try
    {
        m_ringBytes = std::clamp (bufferBytes, MIN_BUFFER_MB * 1024 * 1024, MAX_BUFFER_MB * 1024 * 1024);
        m_ring = std::make_unique_for_overwrite<char[]> (m_ringBytes);
    }
    catch (const std::bad_alloc &)
    {
        Say (std::cerr) << "proxy: could not set aside " << m_ringBytes / (1024 * 1024) << " MB for the stream\n";
        m_ringBytes = 0;
        return (false);
    }

    m_listenFd = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (m_listenFd < 0)
    {
        Say (std::cerr) << "proxy: could not create socket\n";
        return (false);
    }

    const int one = 1;

    setsockopt (m_listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one));

    struct sockaddr_in addr;

    std::memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_ANY);
    addr.sin_port = htons (static_cast<uint16_t> (port));

    if (bind (m_listenFd, reinterpret_cast<struct sockaddr *> (&addr), sizeof (addr)) < 0)
    {
        Say (std::cerr) << "proxy: could not bind port " << port << ": " << std::strerror (errno) << "\n";
        close (m_listenFd);
        m_listenFd = -1;
        return (false);
    }

    if (listen (m_listenFd, 8) < 0)
    {
        Say (std::cerr) << "proxy: listen failed\n";
        close (m_listenFd);
        m_listenFd = -1;
        return (false);
    }

    m_speakerIp = speakerIp;
    m_port = port;
    m_stopping = false;
    m_running = true;
    m_pumpThread = std::thread (&StreamProxy::pumpLoop, this);
    m_acceptThread = std::thread (&StreamProxy::acceptLoop, this);

    return (true);
}

void StreamProxy::stop ()
{
    if (!m_running)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock (m_mutex);

        m_stopping = true;
    }

    m_changed.notify_all ();

    if (m_acceptThread.joinable ())
    {
        m_acceptThread.join ();
    }

    // Only the accept thread touches the client list, and it has finished.
    reapClients (true);

    if (m_pumpThread.joinable ())
    {
        m_pumpThread.join ();
    }

    if (m_listenFd >= 0)
    {
        close (m_listenFd);
        m_listenFd = -1;
    }

    m_running = false;
}

std::uint64_t StreamProxy::setUpstream (const std::string &url)
{
    std::uint64_t generation = 0;

    {
        std::lock_guard<std::mutex> lock (m_mutex);

        // A fresh play replaces any resume a song change left waiting, and starts a speaker that
        // was waiting to be.
        m_resumePending = false;
        m_restartPending = false;

        if (m_upstream == url && m_generation != 0)
        {
            return (m_generation);
        }

        m_upstream = url;
        m_generation = m_generation + 1;
        m_generationStart = m_written;
        m_titles.clear ();
        m_contentType.clear ();
        m_bitrateKbps = 0;
        m_measuredRate = 0;
        m_rateBaseSet = false;

        generation = m_generation;
    }

    m_changed.notify_all ();

    return (generation);
}

bool StreamProxy::waitForTitle (std::uint64_t generation, std::chrono::milliseconds timeout,
                                TitleMark &mark, std::uint64_t &startOffset, bool &resumes)
{
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now () + timeout;

    std::unique_lock<std::mutex> lock (m_mutex);

    // A station whose headers carry no icy-metaint will never send a title.
    m_changed.wait_until (lock, deadline, [this, generation]
    {
        return (m_stopping || m_generation != generation || !m_titles.empty ()
                || (m_headersGeneration == generation && !m_metadataOffered));
    });

    // The station opens with a burst of backlog that can begin in one song and end in the next.
    // Wait for the burst to finish, so the newest title in it is the one playing now.
    const long rate = bytesPerSecondLocked ();

    if (rate > 0 && !m_titles.empty ())
    {
        double excess = burstExcessLocked (rate);
        std::chrono::steady_clock::time_point grew = std::chrono::steady_clock::now ();

        while (!m_stopping && m_generation == generation)
        {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now ();

            if (now >= deadline || now - m_connectedAt >= BURST_LIMIT || now - grew >= BURST_SETTLE)
            {
                break;
            }

            m_changed.wait_until (lock, std::min (deadline, now + std::chrono::milliseconds (50)));

            const double latest = burstExcessLocked (rate);

            if (latest > excess + BURST_SLACK)
            {
                excess = latest;
                grew = std::chrono::steady_clock::now ();
            }
        }
    }

    if (m_stopping || m_generation != generation)
    {
        return (false);
    }

    startOffset = 0;
    resumes = false;

    // The speaker is on this station, or left it moments ago: carry on where it stopped, under
    // the title in force there, so a re-press replays and skips no more than a fraction of a
    // second. That works whether the station sends titles or not.
    std::uint64_t reached = speakerReachedLocked (generation, std::chrono::steady_clock::now ());

    if (reached != 0 && reached >= resumeFloorLocked ())
    {
        startOffset = reached;
        resumes = true;
        mark = titleAtLocked (reached);

        return (true);
    }

    // It had fallen further behind than the relay holds: start on the song now playing instead,
    // from its beginning.
    if (reached != 0)
    {
        const CatchUp catchUp = catchUpLocked ();

        startOffset = catchUp.start;
        mark = catchUp.mark;

        return (true);
    }

    if (m_titles.empty ())
    {
        return (false);
    }

    // Otherwise the newest title is the song playing now. If it began inside the backlog a
    // fresh connection would be given, the speaker can start right on it instead of playing out
    // the end of the song before, as long as enough of it is here to start on promptly.
    // Otherwise the backlog plays from its start, already showing the new title.
    const std::uint64_t start = backlogStartLocked ();
    const std::uint64_t minimumLead = static_cast<std::uint64_t> (bytesPerSecondLocked ()) * MIN_START_LEAD_SECONDS;

    mark = m_titles.back ();

    // The lead counts from where it would really start, which a positive title offset moves on.
    const std::uint64_t begins = songStartLocked (m_titles.size () - 1);

    if (m_titles.size () > 1 && mark.offset > start && minimumLead > 0
        && m_written >= begins + minimumLead)
    {
        startOffset = begins;
    }

    return (true);
}

// Where the speaker is on this station, or where it stopped moments ago; 0 when there is no telling.
// That takes the rate, without which there is no telling where it got to.
std::uint64_t StreamProxy::speakerReachedLocked (std::uint64_t generation, std::chrono::steady_clock::time_point now,
                                                bool anyAge) const
{
    if (bytesPerSecondLocked () <= 0)
    {
        return (0);
    }

    // A re-press carries on only from a stop moments ago (RESUME_WINDOW); a skip, from wherever
    // the speaker last was, however long ago, as from a long pause.
    // Sent somewhere it has not got to yet, as when a press stopped it before it did.
    if (m_intendedPending && m_intendedGeneration == generation && (anyAge || now - m_intendedTime < RESUME_WINDOW))
    {
        return (m_intendedAt);
    }

    if (m_speakerClient != nullptr && m_speakerGeneration == generation)
    {
        // Still connected. Stopped by a press long enough ago, it is no longer a re-press.
        if (anyAge || !m_speakerStopped || now - m_speakerStoppedTime < RESUME_WINDOW)
        {
            return (speakerEstimateLocked (now));
        }

        return (0);
    }

    if (m_speakerLeftGeneration == generation && (anyAge || now - m_speakerLeftTime < RESUME_WINDOW))
    {
        return (m_speakerLeftAt);
    }

    return (0);
}

bool StreamProxy::skipFrom (std::uint64_t generation, int steps, bool again, Skip &skip) const
{
    std::lock_guard<std::mutex> lock (m_mutex);

    const long rate = bytesPerSecondLocked ();

    if (generation != m_generation || rate <= 0)
    {
        return (false);
    }

    std::uint64_t reached = speakerReachedLocked (generation, std::chrono::steady_clock::now (), true);

    if (reached == 0)
    {
        return (false);
    }

    const std::uint64_t floor = resumeFloorLocked ();
    const std::uint64_t landing = static_cast<std::uint64_t> (rate) * FLOOR_LANDING_SECONDS;

    // Further behind than the relay holds, there is nothing to step from: catch up instead. Just
    // past the edge, as after landing near it, it still counts as at it.
    if (reached + landing < floor)
    {
        const CatchUp catchUp = catchUpLocked ();

        skip = Skip { catchUp.start, catchUp.mark, 0, false, true };

        return (true);
    }

    reached = std::max (reached, floor);

    skip = Skip { reached, titleAtLocked (reached), 0, false, false };

    if (steps == 0)
    {
        return (true);
    }

    // The first metadata block's title names a song already under way when the station was joined:
    // its start is not held, only the station's first byte.
    const std::uint64_t firstBlockEnd = m_generationStart + static_cast<std::uint64_t> (m_metaInterval);

    if (steps > 0)
    {
        const std::uint64_t lead = static_cast<std::uint64_t> (rate) * MIN_START_LEAD_SECONDS;

        for (size_t index = 0; index < m_titles.size (); ++index)
        {
            const TitleMark &mark = m_titles[index];
            const std::uint64_t begins = songStartLocked (index);

            // Only songs that begin ahead of where it is: one it is already hearing is this one.
            if (begins <= reached || mark.offset <= firstBlockEnd)
            {
                continue;
            }

            // Too little of it here to start on promptly, and every later one is newer still.
            if (m_written - begins < lead)
            {
                break;
            }

            skip.start = begins;
            skip.mark = mark;

            if (++skip.songs == steps)
            {
                break;
            }
        }

        return (true);
    }

    // Back: the songs at or before where it is, newest first, each where it starts. None starts
    // before the floor; one that began there or before the station was joined starts at the
    // oldest byte held, and nothing older is.
    struct Start
    {
        std::uint64_t at;
        TitleMark     mark;
        bool          partial;
    };

    std::vector<Start> starts;

    for (size_t index = m_titles.size (); index-- > 0; )
    {
        const TitleMark &mark = m_titles[index];
        const bool underWay = (mark.offset <= firstBlockEnd);
        const std::uint64_t begins = underWay ? m_generationStart : songStartLocked (index);

        // A song that begins ahead of where it is, is still to come.
        if (begins > reached)
        {
            continue;
        }

        // Its start is gone: land inside what is held, clear of the edge, but not past where it is.
        if (begins < floor)
        {
            starts.push_back (Start { std::min (floor + landing, reached), mark, true });
            break;
        }

        if (underWay)
        {
            starts.push_back (Start { begins, mark, true });
            break;
        }

        starts.push_back (Start { begins, mark, false });
    }

    if (starts.empty ())
    {
        const std::uint64_t begins = (m_generationStart >= floor) ? m_generationStart : std::min (floor + landing, reached);

        starts.push_back (Start { begins, skip.mark, true });
    }

    // Far enough into the song it is in, the first step goes back to its start; otherwise, or
    // pressed again straight after going back, to the song before, when there is one.
    size_t first = (!again && reached - starts.front ().at >= static_cast<std::uint64_t> (rate) * SKIP_RESTART_SECONDS) ? 0 : 1;

    first = std::min (first, starts.size () - 1);

    const size_t index = std::min (first + static_cast<size_t> (-steps) - 1, starts.size () - 1);

    skip.start = starts[index].at;
    skip.mark = starts[index].mark;
    skip.partial = starts[index].partial;
    skip.songs = static_cast<int> (index);

    return (true);
}

StreamProxy::Timeline StreamProxy::getTimeline (std::uint64_t generation, std::uint64_t after) const
{
    Timeline timeline {};

    std::lock_guard<std::mutex> lock (m_mutex);

    timeline.generation = m_generation;
    timeline.written = m_written;
    timeline.bytesPerSecond = bytesPerSecondLocked ();

    if (generation != m_generation)
    {
        return (timeline);
    }

    timeline.speakerAttached = (m_speakerClient != nullptr && m_speakerGeneration == generation);
    timeline.speakerStopped = m_speakerStopped;
    timeline.speakerConnections = m_speakerConnections;
    timeline.speakerConnectedAt = m_speakerConnectedAt;
    timeline.resumeFloor = resumeFloorLocked ();
    timeline.catchUp = catchUpLocked ();
    timeline.speakerNeedsRestart = (m_restartPending && m_restartGeneration == generation);
    timeline.titlesOffered = !(m_headersGeneration == generation && !m_metadataOffered);

    if (timeline.speakerAttached)
    {
        timeline.speakerPosition = speakerEstimateLocked (std::chrono::steady_clock::now ());
    }

    for (const TitleMark &mark : m_titles)
    {
        if (mark.offset > after)
        {
            timeline.upcoming.push_back (mark);
        }
    }

    return (timeline);
}

void StreamProxy::noteSpeakerStopped (std::chrono::steady_clock::time_point at)
{
    std::lock_guard<std::mutex> lock (m_mutex);

    const bool rateKnown = (bytesPerSecondLocked () > 0);

    if (m_speakerClient != nullptr && !m_speakerStopped)
    {
        m_speakerStoppedAt = rateKnown ? speakerEstimateLocked (at) : 0;
        m_speakerStoppedTime = at;
        m_speakerStopped = true;
    }
    else if (m_speakerClient == nullptr && m_speakerLeftGeneration == m_generation && m_speakerLeftAt != 0
             && m_speakerLeftTime > at && rateKnown)
    {
        // It hung up after it had stopped, so it left off where it stopped, not where it was
        // last seen. Its connection's numbers still give where that was.
        m_speakerLeftAt = std::min (m_speakerLeftAt, speakerEstimateLocked (at));
        m_speakerLeftTime = at;
    }
}

std::uint64_t StreamProxy::speakerConnections () const
{
    std::lock_guard<std::mutex> lock (m_mutex);

    return (m_speakerConnections);
}

void StreamProxy::noteSpeakerResumed ()
{
    std::lock_guard<std::mutex> lock (m_mutex);

    // Played again with no connection: it plays what it holds and asks for more only when that runs
    // out, so its next connection must count from this moment, not from where it stopped.
    if (m_speakerClient == nullptr && m_speakerLeftGeneration == m_generation)
    {
        m_resumedWhileGone = true;
        m_resumedWhileGoneTime = std::chrono::steady_clock::now ();
    }

    if (m_speakerClient != nullptr && m_speakerStopped)
    {
        // Count on from the held position, as if the connection had begun there just now.
        if (m_speakerStoppedAt != 0)
        {
            m_speakerStart = m_speakerStoppedAt;
        }

        m_speakerSince = std::chrono::steady_clock::now () - SPEAKER_STARTUP;
        m_speakerStopped = false;
    }
}

void StreamProxy::resumeAt (std::uint64_t generation, std::uint64_t offset)
{
    std::lock_guard<std::mutex> lock (m_mutex);

    if (generation == m_generation)
    {
        m_resumePending = true;
        m_resumeGeneration = generation;
        m_resumeOffset = offset;
        m_restartPending = false;

        m_intendedPending = true;
        m_intendedAt = offset;
        m_intendedGeneration = generation;
        m_intendedTime = std::chrono::steady_clock::now ();
    }
}

void StreamProxy::cancelRestart ()
{
    std::lock_guard<std::mutex> lock (m_mutex);

    m_restartPending = false;
}

void StreamProxy::setTitleOffset (double seconds)
{
    std::lock_guard<std::mutex> lock (m_mutex);

    m_titleOffsetSeconds = seconds;
}

void StreamProxy::cancelResume ()
{
    std::lock_guard<std::mutex> lock (m_mutex);

    m_resumePending = false;
    m_intendedPending = false;
}

std::string StreamProxy::urlForSpeaker (const std::string &streamName) const
{
    // Ask the kernel which local address it would use to reach the speaker, so this works
    // whatever interface is in play. No packets are sent — UDP connect only sets the route.
    std::string local = "127.0.0.1";

    const int probe = socket (AF_INET, SOCK_DGRAM, 0);

    if (probe >= 0)
    {
        struct sockaddr_in to;

        std::memset (&to, 0, sizeof (to));
        to.sin_family = AF_INET;
        to.sin_port = htons (8090);
        to.sin_addr.s_addr = inet_addr (m_speakerIp.c_str ());

        if (connect (probe, reinterpret_cast<struct sockaddr *> (&to), sizeof (to)) == 0)
        {
            struct sockaddr_in me;
            socklen_t length = sizeof (me);

            if (getsockname (probe, reinterpret_cast<struct sockaddr *> (&me), &length) == 0)
            {
                char text[INET_ADDRSTRLEN] = {};

                if (inet_ntop (AF_INET, &me.sin_addr, text, sizeof (text)) != nullptr)
                {
                    local = text;
                }
            }
        }

        close (probe);
    }

    return ("http://" + local + ":" + std::to_string (m_port) + "/stream/" + percentEncode (streamName));
}

std::string StreamProxy::streamNameFromUrl (const std::string &url, int port)
{
    const std::string scheme = "http://";

    if (url.compare (0, scheme.size (), scheme) != 0)
    {
        return ("");
    }

    const size_t slash = url.find ('/', scheme.size ());

    if (slash == std::string::npos)
    {
        return ("");
    }

    const std::string authority = url.substr (scheme.size (), slash - scheme.size ());
    const std::string portSuffix = ":" + std::to_string (port);

    if (port > 0 && (authority.size () <= portSuffix.size ()
        || authority.compare (authority.size () - portSuffix.size (), portSuffix.size (), portSuffix) != 0))
    {
        return ("");
    }

    const std::string prefix = "/stream/";

    if (url.compare (slash, prefix.size (), prefix) != 0)
    {
        return ("");
    }

    return (percentDecode (url.substr (slash + prefix.size ())));
}
