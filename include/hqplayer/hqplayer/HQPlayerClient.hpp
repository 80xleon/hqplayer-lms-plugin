#pragma once

#include "hqplayer/config/Config.hpp"
#include "hqplayer/hqplayer/IHQPlayerClient.hpp"

#include <mutex>

namespace hqplayer::hqplayer {

/// HQPlayer Embedded XML/TCP control client.
///
/// Communicates with HQPlayer Embedded via its XML control protocol on
/// port 4321 (configurable).  Each public method opens a dedicated TCP
/// connection, sends an XML command, reads the response, and closes the
/// connection.  This stateless approach tolerates HQPlayer restarts
/// without explicit reconnect logic.
///
/// All public methods are thread-safe.
class HQPlayerClient final : public IHQPlayerClient {
public:
    /// Construct the client with the given configuration.
    explicit HQPlayerClient(const config::HQPlayerConfig& config);
    ~HQPlayerClient() override = default;

    /// Start HQPlayer playback.
    /// @throws HQPlayerError on network or protocol error.
    void play() override;

    /// Pause HQPlayer playback.
    /// @throws HQPlayerError on network or protocol error.
    void pause() override;

    /// Stop HQPlayer playback.
    /// @throws HQPlayerError on network or protocol error.
    void stop() override;

    /// Skip to the next track in the current HQPlayer playlist.
    /// @throws HQPlayerError on network or protocol error.
    void next() override;

    /// Skip to the previous track in the current HQPlayer playlist.
    /// @throws HQPlayerError on network or protocol error.
    void prev() override;

    /// Queue @p uri as the next file to play (HQPe --play-next-uri semantics).
    ///
    /// Sends <PlayNextUri uri="<uri>"/> to HQPlayer Embedded, optionally
    /// including song/artist/album metadata attributes from @p meta.
    /// See IHQPlayerClient::playNextUri for full semantics.
    /// @throws HQPlayerError on network or protocol error.
    void playNextUri(const std::string& uri,
                     const TrackMetadata& meta = {}) override;

    /// Load a single audio file and begin playback via HQPlayer.
    ///
    /// Sends <Load src="<filePath>"/> then <Play/> to HQPlayer Embedded.
    /// Superseded by playNextUri() for LMS-driven playback.
    /// @throws HQPlayerError on network or protocol error.
    void loadTrack(const std::string& filePath) override;

    /// Query the current HQPlayer status.
    /// @return Parsed HQPlayerStatus.
    /// @throws HQPlayerError on network or protocol error.
    HQPlayerStatus getStatus() override;

private:
    /// Open a TCP connection, send @p xmlCommand, read full response and return it.
    /// The XML declaration is prepended automatically.
    std::string sendAndReceive(const std::string& xmlCommand) const;

    /// Parse a {@code <Status .../>} XML string into HQPlayerStatus.
    static HQPlayerStatus parseStatusXml(const std::string& xml);

    /// Parse a transport command result XML and throw HQPlayerError on failure.
    static void verifyResult(const std::string& xml, const std::string& commandName);

    config::HQPlayerConfig config_;
    mutable std::mutex     mutex_;
};

} // namespace hqplayer::hqplayer
