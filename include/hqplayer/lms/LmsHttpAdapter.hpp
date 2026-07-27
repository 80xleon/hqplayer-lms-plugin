#pragma once

#include "hqplayer/lms/LmsBridge.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace hqplayer::lms {

class LmsHttpAdapter final {
public:
    LmsHttpAdapter(LmsBridge& bridge, std::string host, std::uint16_t port);
    ~LmsHttpAdapter();

    void start();
    void stop();

    [[nodiscard]] std::uint16_t boundPort() const;

private:
    void run();
    std::string statusJson() const;

    LmsBridge& bridge_;
    std::string host_;
    std::uint16_t port_;

    mutable std::mutex state_mutex_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::uint16_t bound_port_{0};

    std::condition_variable start_cv_;
    bool start_done_{false};
    std::string start_error_;
};

} // namespace hqplayer::lms
