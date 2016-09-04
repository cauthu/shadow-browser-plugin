
#include "socks5_connector.hpp"
#include "easylogging++.h"

namespace myio
{

Socks5Connector::Socks5Connector(StreamChannel::UniquePtr transport,
                                 const in_addr_t target_host, uint16_t port)
    : state_(SOCKS5_NONE), transport_(std::move(transport))
    , target_host_(target_host), port_(port)
{
    CHECK_EQ(transport_->get_avail_input_length(), 0);
    CHECK_EQ(transport_->get_output_length(), 0);
}

int
Socks5Connector::start_connecting(StreamChannelConnectObserver* observer)
{
    CHECK_EQ(state_, SOCKS5_NONE);
    observer_ = observer;
    transport_->set_observer(this);

    // write the client greeting
    static const char req[] = "\x05\x01\x00";

    // don't use "sizeof req", which gives you 4
    auto rv = transport_->write(req, 3);
    CHECK_EQ(rv, 0);

    state_ = State::SOCKS5_GREETING;

    return 0;
}

void
Socks5Connector::onNewReadDataAvailable(StreamChannel* ch) noexcept
{
    CHECK_EQ(transport_.get(), ch);
    _consume_input();
}

void
Socks5Connector::_consume_input()
{
    VLOG(2) << "begin";

    DestructorGuard dg(this);

    switch (state_) {
    case State::SOCKS5_GREETING:
        // this should be the first accept response from socks5 proxy
        char mem[2] = {0};
        auto rv = transport_->read(mem, sizeof mem);
        CHECK_EQ(rv, sizeof mem);

        if (memcmp(mem, "\x05\x00", sizeof mem)) {
            observer_->onSocksTargetConnectResult(this, ERR_FAIL);
            state_ = State::SOCKS5_DONE;
            return;
        }

        VLOG(2) << "write the connect request";

        // const char namelen = target_host_.length();
        const uint16_t port = htons(port_);

        std::string req("\x05\x01\x00\x01", 4);
        req.append((const char*)&target_host_, 4);
        req.append((const char*)&port, 2);

        rv = transport_->write(req.c_str(), req.size());
        CHECK_EQ(rv, 0);

        VLOG(2) << "transport accepted the write -> change state";
        state_ = State::SOCKS5_READ_RESP_NEXT;

        break;

    case State::SOCKS5_READ_RESP_NEXT:
        // assume that proxy response is 10 bytes, i.e., it has
        // connected to ipv4 address
        unsigned char mem[10] = {0};
        const auto rv = transport_->read(mem, sizeof mem);
        CHECK_EQ(rv, sizeof mem);

        // only check the first 4 bytes for status. ignore the rest
        if (memcmp(mem, "\x05\x00\x00\x01", 4)) {
            observer_->onSocksTargetConnectResult(this, ERR_FAIL);
            state_ = State::SOCKS5_DONE;
            return;
        }

        state_ = State::SOCKS5_DONE;
        observer_->onSocksTargetConnectResult(this, OK);

        break;

    default:
        CHECK(false) << "invalid state: " << state_;
        break;
    }

    VLOG(2) << "done";
}

}
