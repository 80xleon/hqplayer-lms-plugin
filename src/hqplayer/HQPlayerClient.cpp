#include "hqplayer/hqplayer/HQPlayerClient.hpp"

#include "hqplayer/util/Logger.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <array>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>

namespace hqplayer::hqplayer {
namespace {

using ::hqplayer::util::LogLevel;
using ::hqplayer::util::Logger;

constexpr const char* kXmlDecl = R"(<?xml version="1.0" encoding="UTF-8"?>)";

/// Escape a string for use as a double-quoted XML attribute value.
/// Replaces &, <, >, and " with the corresponding XML entities.
std::string xmlAttrEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        default:   out += c;        break;
        }
    }
    return out;
}

/// Read from @p socket until EOF, timeout, or error.
/// Returns accumulated bytes as a string.
std::string readAll(boost::asio::ip::tcp::socket& socket) {
    std::string result;
    std::array<char, 4096> buf{};
    boost::system::error_code ec;

    for (;;) {
        const std::size_t n = socket.read_some(boost::asio::buffer(buf), ec);
        if (n > 0) {
            result.append(buf.data(), n);
        }
        if (ec) {
            break;
        }
    }

    // EOF and connection-reset are expected end-of-response signals.
    if (ec != boost::asio::error::eof &&
        ec != boost::asio::error::connection_reset &&
        ec.value() != 0) {
        // EAGAIN/EWOULDBLOCK means read timed out — acceptable if we got data.
        if (ec.value() != EAGAIN && ec.value() != EWOULDBLOCK) {
            Logger::instance().log(LogLevel::Debug,
                "HQPlayer read ended: " + ec.message());
        }
    }

    return result;
}

/// Strip the leading {@code <?xml ... ?>} declaration from @p xml so that
/// Boost.PropertyTree can parse the remaining element without complaints.
std::string stripXmlDecl(const std::string& xml) {
    const auto pos = xml.find("?>");
    if (pos == std::string::npos) {
        return xml;
    }
    std::string stripped = xml.substr(pos + 2);
    // Trim leading whitespace.
    const auto first = stripped.find_first_not_of(" \t\r\n");
    return (first == std::string::npos) ? stripped : stripped.substr(first);
}

/// Parse a single-element XML string via Boost.PropertyTree.
/// Returns the ptree of the root element.
boost::property_tree::ptree parseXml(const std::string& xml) {
    const std::string body = stripXmlDecl(xml);
    std::istringstream iss(body);
    boost::property_tree::ptree pt;
    try {
        boost::property_tree::read_xml(iss, pt);
    } catch (const boost::property_tree::xml_parser_error& e) {
        throw HQPlayerError("XML parse error: " + std::string(e.what()));
    }
    return pt;
}

/// Safe int conversion from ptree attribute string.
int safeInt(const std::string& s, int def = 0) noexcept {
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

/// Safe float conversion from ptree attribute string.
float safeFloat(const std::string& s, float def = 0.0f) noexcept {
    try {
        return std::stof(s);
    } catch (...) {
        return def;
    }
}

} // namespace

HQPlayerClient::HQPlayerClient(const config::HQPlayerConfig& config)
    : config_(config) {}

std::string HQPlayerClient::sendAndReceive(const std::string& xmlCommand) const {
    namespace asio = boost::asio;
    using tcp = asio::ip::tcp;

    std::lock_guard<std::mutex> lock(mutex_);

    asio::io_context ioc;
    tcp::socket socket(ioc);

    // Resolve host.
    boost::system::error_code ec;
    tcp::resolver resolver(ioc);
    const auto endpoints = resolver.resolve(config_.host, std::to_string(config_.port), ec);
    if (ec) {
        throw HQPlayerError(
            "Cannot resolve HQPlayer host '" + config_.host + "': " + ec.message());
    }

    // Connect.
    asio::connect(socket, endpoints, ec);
    if (ec) {
        throw HQPlayerError(
            "Cannot connect to HQPlayer " + config_.host + ":" +
            std::to_string(config_.port) + ": " + ec.message());
    }

    // Apply socket-level timeout so blocked reads/writes never hang forever.
    struct timeval tv{};
    tv.tv_sec  = static_cast<time_t>(config_.timeout_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((config_.timeout_ms % 1000) * 1000);
    ::setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(socket.native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Send: XML declaration + command body.
    const std::string fullCommand = std::string(kXmlDecl) + xmlCommand;
    asio::write(socket, asio::buffer(fullCommand), ec);
    if (ec) {
        throw HQPlayerError("Send to HQPlayer failed: " + ec.message());
    }

    Logger::instance().log(LogLevel::Debug, "HQPlayer >> " + xmlCommand);

    // Read the response.
    std::string response = readAll(socket);
    socket.shutdown(tcp::socket::shutdown_both, ec);
    socket.close(ec);

    if (response.empty()) {
        throw HQPlayerError("Empty response from HQPlayer for command: " + xmlCommand);
    }

    Logger::instance().log(LogLevel::Debug,
        "HQPlayer << " + response.substr(0, std::min(response.size(), std::size_t{200})));

    return response;
}

// static
HQPlayerStatus HQPlayerClient::parseStatusXml(const std::string& xml) {
    const auto pt = parseXml(xml);

    // The root element is <Status .../>
    const auto statusOpt = pt.get_child_optional("Status");
    if (!statusOpt) {
        throw HQPlayerError("Status XML missing <Status> root element");
    }
    const auto& statusNode = *statusOpt;

    HQPlayerStatus result{};

    const int rawState = safeInt(statusNode.get<std::string>("<xmlattr>.state", "0"));
    switch (rawState) {
    case 1:  result.state = HQPlayerState::Playing; break;
    case 2:  result.state = HQPlayerState::Paused;  break;
    default: result.state = HQPlayerState::Stopped; break;
    }

    result.samplerate_hz = static_cast<std::uint32_t>(
        safeInt(statusNode.get<std::string>("<xmlattr>.active_rate", "0")));

    result.bitdepth = static_cast<std::uint32_t>(
        safeInt(statusNode.get<std::string>("<xmlattr>.active_bits", "0")));

    result.is_dsd = (result.bitdepth == 1);

    result.volume_db = safeFloat(statusNode.get<std::string>("<xmlattr>.volume", "0"));

    // Optional <metadata> child — present when a track is loaded.
    const auto metaOpt = statusNode.get_child_optional("metadata");
    if (metaOpt) {
        result.track_title = metaOpt->get<std::string>("<xmlattr>.song", "");
        result.artist      = metaOpt->get<std::string>("<xmlattr>.artist", "");
        result.album       = metaOpt->get<std::string>("<xmlattr>.album", "");
    }

    return result;
}

// static
void HQPlayerClient::verifyResult(const std::string& xml, const std::string& commandName) {
    const auto pt = parseXml(xml);

    // The root element name matches the command (e.g. <Play result="OK"/>).
    // Iterate over the single root child to get its result attribute.
    for (const auto& [tag, node] : pt) {
        const std::string resultAttr = node.get<std::string>("<xmlattr>.result", "");
        if (resultAttr == "OK") {
            return; // success
        }
        const std::string errorText = node.get_value<std::string>("");
        throw HQPlayerError(
            commandName + " command rejected by HQPlayer: " +
            (errorText.empty() ? resultAttr : errorText));
    }
    // No root element found — treat as success (HQPlayer may send no body).
}

void HQPlayerClient::play() {
    Logger::instance().log(LogLevel::Info, "Sending Play to HQPlayer");
    const auto response = sendAndReceive("<Play/>");
    verifyResult(response, "Play");
}

void HQPlayerClient::pause() {
    Logger::instance().log(LogLevel::Info, "Sending Pause to HQPlayer");
    const auto response = sendAndReceive("<Pause/>");
    verifyResult(response, "Pause");
}

void HQPlayerClient::stop() {
    Logger::instance().log(LogLevel::Info, "Sending Stop to HQPlayer");
    const auto response = sendAndReceive("<Stop/>");
    verifyResult(response, "Stop");
}

void HQPlayerClient::next() {
    Logger::instance().log(LogLevel::Info, "Sending Next to HQPlayer");
    const auto response = sendAndReceive("<Next/>");
    verifyResult(response, "Next");
}

void HQPlayerClient::prev() {
    Logger::instance().log(LogLevel::Info, "Sending Prev to HQPlayer");
    const auto response = sendAndReceive("<Prev/>");
    verifyResult(response, "Prev");
}

void HQPlayerClient::playNextUri(const std::string& uri, const TrackMetadata& meta) {
    if (uri.empty()) {
        throw HQPlayerError("playNextUri: uri must not be empty");
    }

    Logger::instance().log(LogLevel::Info, "Queuing next URI in HQPlayer: " + uri);

    // HQPlayer Embedded XML/TCP API: queue the next file via
    // <PlayNextUri uri="<path>"/>.  This mirrors the --play-next-uri CLI
    // option:
    //  - When stopped:  starts playback of <uri> immediately.
    //  - When playing:  queues <uri> for gapless transition after current
    //                   track ends.
    //
    // Optional metadata attributes (song, artist, album) let HQPlayer display
    // the Now Playing information without re-reading the file tags.
    //
    // Expected response: <PlayNextUri result="OK"/>
    std::string cmd = "<PlayNextUri uri=\"" + uri + "\"";
    if (!meta.title.empty())  cmd += " song=\""   + xmlAttrEscape(meta.title)  + "\"";
    if (!meta.artist.empty()) cmd += " artist=\"" + xmlAttrEscape(meta.artist) + "\"";
    if (!meta.album.empty())  cmd += " album=\""  + xmlAttrEscape(meta.album)  + "\"";
    cmd += "/>";

    const auto response = sendAndReceive(cmd);
    verifyResult(response, "PlayNextUri");
}

void HQPlayerClient::loadTrack(const std::string& filePath) {
    if (filePath.empty()) {
        throw HQPlayerError("loadTrack: filePath must not be empty");
    }

    Logger::instance().log(LogLevel::Info, "Loading track in HQPlayer: " + filePath);

    // HQPlayer Embedded XML/TCP API: load a file via <Load src="<path>"/>.
    // The src attribute accepts an absolute filesystem path.  Both the LMS
    // host and the HQPlayer Embedded host must have access to the same path
    // (e.g. via a shared NAS mount).
    //
    // Expected response: <Load result="OK"/>
    const std::string loadCmd = "<Load src=\"" + filePath + "\"/>";
    const auto loadResponse = sendAndReceive(loadCmd);
    verifyResult(loadResponse, "Load");

    // Start playback immediately after loading.
    const auto playResponse = sendAndReceive("<Play/>");
    verifyResult(playResponse, "Play");
}

HQPlayerStatus HQPlayerClient::getStatus() {
    const auto response = sendAndReceive("<Status/>");
    return parseStatusXml(response);
}

} // namespace hqplayer::hqplayer
