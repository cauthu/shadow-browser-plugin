
#include <boost/bind.hpp>
#include <arpa/inet.h>

#include "client_handler.hpp"


#define _LOG_PREFIX(inst) << "chandler= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)



using myio::StreamChannel;
using myio::buflo::BufloMuxChannel;


ClientHandler::ClientHandler(StreamChannel::UniquePtr client_channel,
                             BufloMuxChannel* buflo_channel,
                             HandlerDoneCb handler_done_cb)
    : client_channel_(std::move(client_channel))
    , buflo_channel_(buflo_channel)
    , state_(State::READ_SOCKS5_GREETING)
    , sid_(-1)
    , handler_done_cb_(handler_done_cb)
{
    client_channel_->set_observer(this);
}

void
ClientHandler::onStreamIdAssigned(BufloMuxChannel*,
                                  int sid) noexcept
{
    sid_ = sid;
}

void
ClientHandler::onStreamCreateResult(BufloMuxChannel*,
                                    bool ok) noexcept
{
    CHECK_EQ(state_, State::CREATE_BUFLO_STREAM);
    if (ok) {
        CHECK_GT(sid_, -1);
        state_ = State::FORWARDING;

        // hand off the the two streams to inner outer handler to do
        // the forwarding
        inner_outer_handler_.reset(
            new InnerOuterHandler(
                std::move(client_channel_), sid_, buflo_channel_,
                boost::bind(&ClientHandler::_on_inner_outer_handler_done,
                            this, _1)));
        buflo_channel_ = nullptr;
    } else {
        _close(true);
    }
}

void
ClientHandler::_on_inner_outer_handler_done(InnerOuterHandler*)
{
    _close(true);
}

void
ClientHandler::onStreamNewDataAvailable(BufloMuxChannel*) noexcept
{
    logself(FATAL) << "not reached";
}

void
ClientHandler::onStreamClosed(BufloMuxChannel*) noexcept
{
    CHECK_EQ(state_, State::CREATE_BUFLO_STREAM);
    _close(true);
}


void
ClientHandler::onNewReadDataAvailable(StreamChannel* ch) noexcept
{
    // read data from client
    _consume_client_input();
}

bool
ClientHandler::_read_socks5_greeting(size_t num_avail_bytes)
{
    static const unsigned char expected_greeting[] = "\x05\x01\x00";

    if (num_avail_bytes >= 3) {
        vlogself(2) << "read socks5 greeting from client";

        unsigned char buf[3] = {0};
        auto rv = client_channel_->read(buf, 3);
        CHECK_EQ(rv, 3);

        if (!memcmp(buf, expected_greeting, 3)) {
            // valid greeting, so we write the greeting response
            static const unsigned char greeting_resp[] = "\x05\x00";
            rv = client_channel_->write(greeting_resp, 2);
            CHECK_EQ(rv, 0);
            state_ = State::READ_SOCKS5_CONNECT_REQ;
            vlogself(2) << "... good -> send reply greeting";
            return true;
        } else {
            _close(true);
        }
    }

    return false;
}

bool
ClientHandler::_create_stream(const char* host,
                              uint16_t port)
{
    const auto rv = buflo_channel_->create_stream(host, port, this);
    return (rv == 0);
}

bool
ClientHandler::_read_socks5_connect_req(size_t num_avail_bytes)
{
    unsigned char buf[10] = {0};
    if (num_avail_bytes >= (sizeof buf)) {
        vlogself(2) << "read socks5 connect request from client";
        auto rv = client_channel_->read(buf, (sizeof buf));
        CHECK_EQ(rv, (sizeof buf));

        if (!memcmp(buf, "\x05\x01\x00\x01", 4)) {

            // read the addr and port
            in_addr_t target_addr;
            uint16_t port = 0;
            memcpy((uint8_t*)&target_addr, buf+4, 4);
            memcpy((uint8_t*)&port, buf+8, 2);
            port = ntohs(port);

            struct sockaddr_in sa;
            char str[INET_ADDRSTRLEN] = {0};
            sa.sin_addr.s_addr = target_addr;
            inet_ntop(AF_INET, &(sa.sin_addr), str, INET_ADDRSTRLEN);
            vlogself(2) << "connect req: [" << str << "]:" << port;

            auto rv = buflo_channel_->create_stream(str, port, this);
            if (!rv) {
                state_ = State::CREATE_BUFLO_STREAM;
                // we should not consume more input because we're now
                // waiting for stream creation
                return false;
            }
            return true;
        }

        vlogself(2) << "closing down";
        _close(true);
    }

    return false;
}

void
ClientHandler::_consume_client_input()
{
    struct evbuffer* inbuf = client_channel_->get_input_evbuf();
    bool keep_consuming = true;

    do {
        const auto num_avail_bytes = evbuffer_get_length(inbuf);
        if (!num_avail_bytes) {
            break;
        }
        switch (state_) {

        case State::READ_SOCKS5_GREETING: {
            keep_consuming = _read_socks5_greeting(num_avail_bytes);
            break;
        }

        case State::READ_SOCKS5_CONNECT_REQ: {
            keep_consuming = _read_socks5_connect_req(num_avail_bytes);
            break;
        }

        case State::FORWARDING: {
            // the inner outer handler should be getting observing the
            // client channel by now
            logself(FATAL) << "not reached";
            break;
        }

        default:
            logself(FATAL) << "not reached";
        }
    } while (keep_consuming);

}

void
ClientHandler::onWrittenData(StreamChannel* ch) noexcept
{
}

void
ClientHandler::onEOF(StreamChannel*) noexcept
{
    vlogself(2) << "client channel eof";
    CHECK((state_ == State::READ_SOCKS5_CONNECT_REQ) ||
          (state_ == State::READ_SOCKS5_GREETING));
    _close(true);
}

void
ClientHandler::onError(StreamChannel*, int) noexcept
{
    vlogself(2) << "client channel error";
    CHECK((state_ == State::READ_SOCKS5_CONNECT_REQ) ||
          (state_ == State::READ_SOCKS5_GREETING));
    _close(true);
}

void
ClientHandler::_close(bool do_notify)
{
    if (state_ == State::CLOSED) {
        return;
    }

    state_ = State::CLOSED;

    client_channel_.reset();
    inner_outer_handler_.reset();
    if (buflo_channel_) {
        buflo_channel_->set_stream_observer(sid_, nullptr);
        buflo_channel_->close_stream(sid_);
        buflo_channel_ = nullptr;
    }

    if (do_notify) {
        DestructorGuard dg(this);
        handler_done_cb_(this);
    }
}

ClientHandler::~ClientHandler()
{
    vlogself(2) << "clienthandler destructing";
    _close(false);
}
