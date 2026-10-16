#ifndef ICY_DEMUXER_H
#define ICY_DEMUXER_H

#include <string>
#include <functional>
#include <cstddef>

// Splits a Shoutcast-style response into its audio and the song titles interleaved with it.
// A server that honours "Icy-MetaData: 1" inserts a metadata block after every icy-metaint
// bytes of audio; one that does not sends plain audio, which passes straight through.
class IcyDemuxer
{
    public :

        using AudioCallback = std::function<void(const char *, size_t)>;

        // Called for each new title. Return false to stop.
        using TitleCallback = std::function<bool(const std::string &)>;

        IcyDemuxer (AudioCallback onAudio, TitleCallback onTitle);

        // One header line as libcurl delivers it. A status line starts a new response, so a
        // redirect hop's headers never leak into the final one.
        void onHeader (const std::string &line);

        // Returns false once a title callback has asked to stop.
        bool onBody (const char *data, size_t length);

        long               getMetaInterval () const { return (m_metaInterval); }
        long               getBitrateKbps () const  { return (m_bitrateKbps); }
        const std::string &getContentType () const  { return (m_contentType); }

    private :

        bool emitTitle ();

        // Now the data members

        AudioCallback m_onAudio;
        TitleCallback m_onTitle;
        std::string   m_contentType;
        std::string   m_lastTitle;
        std::string   m_metaBuffer;
        long          m_metaInterval;
        long          m_bitrateKbps;
        long          m_audioLeft;
        long          m_metaLeft;
        bool          m_awaitingLength;
};

#endif
