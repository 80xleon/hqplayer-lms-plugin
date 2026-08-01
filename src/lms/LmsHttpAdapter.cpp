#include "hqplayer/lms/LmsHttpAdapter.hpp"

#include "hqplayer/hqplayer/HQPlayerTypes.hpp"
#include "hqplayer/util/Logger.hpp"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <optional>
#include <stdexcept>
#include <string>

namespace hqplayer::lms {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

std::string escapeJson(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (const auto c : in) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

http::response<http::string_body> makeJsonResponse(http::status status, std::string body) {
    http::response<http::string_body> response{status, 11};
    response.set(http::field::content_type, "application/json");
    response.set(http::field::connection, "close");
    response.body() = std::move(body);
    response.prepare_payload();
    return response;
}

/// Extract the value of a JSON string field named @p key from @p body.
/// Handles the common pattern {"key":"value"} without a full JSON library.
/// Returns std::nullopt when the field is absent or malformed.
std::optional<std::string> extractJsonStringField(const std::string& body,
                                                   const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = body.find(needle);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }
    const auto colonPos = body.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) {
        return std::nullopt;
    }
    const auto q1 = body.find('"', colonPos + 1);
    if (q1 == std::string::npos) {
        return std::nullopt;
    }
    const auto q2 = body.find('"', q1 + 1);
    if (q2 == std::string::npos) {
        return std::nullopt;
    }
    return body.substr(q1 + 1, q2 - q1 - 1);
}

} // namespace

LmsHttpAdapter::LmsHttpAdapter(LmsBridge& bridge, std::string host, std::uint16_t port)
    : bridge_(bridge), host_(std::move(host)), port_(port) {}

LmsHttpAdapter::~LmsHttpAdapter() {
    stop();
}

void LmsHttpAdapter::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        start_done_ = false;
        start_error_.clear();
        bound_port_ = 0;
    }

    io_context_.restart();
    worker_ = std::thread(&LmsHttpAdapter::run, this);

    std::unique_lock<std::mutex> lock(state_mutex_);
    start_cv_.wait(lock, [this] { return start_done_; });
    if (!start_error_.empty()) {
        lock.unlock();
        stop();
        throw std::runtime_error(start_error_);
    }
}

void LmsHttpAdapter::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    std::string wake_host;
    std::uint16_t wake_port = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        wake_host = host_;
        wake_port = bound_port_;
        if (acceptor_) {
            boost::system::error_code ec;
            acceptor_->cancel(ec);
        }
    }

    if (wake_port != 0) {
        boost::system::error_code ec;
        const auto address = boost::asio::ip::make_address(wake_host, ec);
        if (!ec) {
            tcp::socket wake_socket(io_context_);
            wake_socket.connect({address, wake_port}, ec);
            if (!ec) {
                wake_socket.shutdown(tcp::socket::shutdown_both, ec);
            }
            wake_socket.close(ec);
        }
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (acceptor_) {
            boost::system::error_code ec;
            acceptor_->close(ec);
        }
    }

    io_context_.stop();

    if (worker_.joinable()) {
        worker_.join();
    }
}

std::uint16_t LmsHttpAdapter::boundPort() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return bound_port_;
}

std::string LmsHttpAdapter::statusJson() const {
    const auto status = bridge_.currentStatus();
    const bool ended  = bridge_.consumeTrackEnded();
    return std::string("{\"state\":\"") + escapeJson(status.state) +
           "\",\"track_title\":\"" + escapeJson(status.track_title) +
           "\",\"samplerate_hz\":" + std::to_string(status.samplerate_hz) +
           ",\"bitdepth\":" + std::to_string(status.bitdepth) +
           ",\"track_ended\":" + (ended ? "true" : "false") + "}";
}

void LmsHttpAdapter::run() {
    namespace http = beast::http;
    using ::hqplayer::util::LogLevel;
    using ::hqplayer::util::Logger;

    try {
        boost::system::error_code ec;
        const auto address = boost::asio::ip::make_address(host_, ec);
        if (ec) {
            throw std::invalid_argument(
                "Invalid lms_adapter.host='" + host_ + "': " + ec.message());
        }

        auto acceptor = std::make_unique<tcp::acceptor>(io_context_);
        acceptor->open(address.is_v6() ? tcp::v6() : tcp::v4(), ec);
        if (ec) {
            throw std::runtime_error("Failed to open LMS adapter socket: " + ec.message());
        }

        acceptor->set_option(boost::asio::socket_base::reuse_address(true), ec);
        if (ec) {
            throw std::runtime_error("Failed to set SO_REUSEADDR: " + ec.message());
        }

        acceptor->bind({address, port_}, ec);
        if (ec) {
            throw std::runtime_error("Failed to bind LMS adapter " + host_ + ":" +
                                     std::to_string(port_) + ": " + ec.message());
        }

        acceptor->listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec) {
            throw std::runtime_error("Failed to listen on LMS adapter socket: " + ec.message());
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            bound_port_ = acceptor->local_endpoint().port();
            acceptor_ = std::move(acceptor);
            start_done_ = true;
        }
        start_cv_.notify_all();

        Logger::instance().log(
            LogLevel::Info,
            "LMS HTTP adapter listening on " + host_ + ":" + std::to_string(boundPort()));

        while (running_.load()) {
            tcp::socket socket(io_context_);
            tcp::acceptor* acceptor = nullptr;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                acceptor = acceptor_.get();
            }
            if (acceptor == nullptr) {
                break;
            }
            acceptor->accept(socket, ec);

            if (ec) {
                if (!running_.load() || ec == boost::asio::error::operation_aborted) {
                    break;
                }
                Logger::instance().log(LogLevel::Warn, "LMS adapter accept failed: " + ec.message());
                continue;
            }

            beast::flat_buffer buffer;
            http::request<http::string_body> request;
            http::read(socket, buffer, request, ec);

            http::response<http::string_body> response;
            if (ec) {
                response = makeJsonResponse(http::status::bad_request, "{\"error\":\"bad request\"}");
            } else if (request.method() == http::verb::get && request.target() == "/lms/status") {
                bridge_.handleCommand(LmsCommand::Status);
                response = makeJsonResponse(http::status::ok, statusJson());
            } else if (request.method() == http::verb::post && request.target() == "/lms/play") {
                bridge_.handleCommand(LmsCommand::Play);
                response = makeJsonResponse(http::status::ok, "{\"ok\":true}");
            } else if (request.method() == http::verb::post && request.target() == "/lms/pause") {
                bridge_.handleCommand(LmsCommand::Pause);
                response = makeJsonResponse(http::status::ok, "{\"ok\":true}");
            } else if (request.method() == http::verb::post && request.target() == "/lms/stop") {
                bridge_.handleCommand(LmsCommand::Stop);
                response = makeJsonResponse(http::status::ok, "{\"ok\":true}");
            } else if (request.method() == http::verb::post && request.target() == "/lms/track") {
                const auto path = extractJsonStringField(request.body(), "path");
                if (!path.has_value() || path->empty()) {
                    response = makeJsonResponse(
                        http::status::bad_request,
                        "{\"error\":\"missing or empty \\\"path\\\" field\"}");
                } else {
                    // Extract optional display metadata — all fields are optional.
                    ::hqplayer::hqplayer::TrackMetadata meta;
                    const auto title    = extractJsonStringField(request.body(), "title");
                    const auto artist   = extractJsonStringField(request.body(), "artist");
                    const auto album    = extractJsonStringField(request.body(), "album");
                    const auto coverart = extractJsonStringField(request.body(), "coverart");
                    if (title.has_value())    meta.title    = *title;
                    if (artist.has_value())   meta.artist   = *artist;
                    if (album.has_value())    meta.album    = *album;
                    if (coverart.has_value()) meta.coverart = *coverart;

                    try {
                        bridge_.handleTrackLoad(*path, meta);
                        response = makeJsonResponse(http::status::ok, "{\"ok\":true}");
                    } catch (const std::invalid_argument& e) {
                        response = makeJsonResponse(
                            http::status::bad_request,
                            "{\"error\":\"" + escapeJson(e.what()) + "\"}");
                    } catch (const std::exception& e) {
                        response = makeJsonResponse(
                            http::status::bad_gateway,
                            "{\"error\":\"" + escapeJson(e.what()) + "\"}");
                    }
                }
            } else if (request.method() == http::verb::post && request.target() == "/lms/album") {
                const auto path = extractJsonStringField(request.body(), "path");
                if (!path.has_value() || path->empty()) {
                    response = makeJsonResponse(
                        http::status::bad_request,
                        "{\"error\":\"missing or empty \\\"path\\\" field\"}");
                } else {
                    try {
                        bridge_.handleAlbumPlay(*path);
                        response = makeJsonResponse(http::status::ok, "{\"ok\":true}");
                    } catch (const std::invalid_argument& e) {
                        response = makeJsonResponse(
                            http::status::bad_request,
                            "{\"error\":\"" + escapeJson(e.what()) + "\"}");
                    } catch (const std::exception& e) {
                        response = makeJsonResponse(
                            http::status::not_implemented,
                            "{\"error\":\"" + escapeJson(e.what()) + "\"}");
                    }
                }
            } else {
                response = makeJsonResponse(http::status::not_found, "{\"error\":\"not found\"}");
            }

            http::write(socket, response, ec);
            socket.shutdown(tcp::socket::shutdown_both, ec);
        }

        Logger::instance().log(LogLevel::Info, "LMS HTTP adapter stopped");
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!start_done_) {
                start_error_ = e.what();
                start_done_ = true;
            }
            acceptor_.reset();
        }
        start_cv_.notify_all();
    }
}

} // namespace hqplayer::lms
