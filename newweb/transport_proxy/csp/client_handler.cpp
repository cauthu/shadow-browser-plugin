
#include <boost/bind.hpp>
#include <arpa/inet.h>
#include <string>

#include "client_handler.hpp"


#define _LOG_PREFIX(inst) << "chandler= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


using std::string;

using myio::StreamChannel;
using myio::buflo::BufloMuxChannel;


namespace csp
{


ClientHandler::ClientHandler(StreamChannel::UniquePtr client_channel,
                             BufloMuxChannel* buflo_channel,
                             ClientHandlerDoneCb handler_done_cb)
    : client_channel_(std::move(client_channel))
    , buflo_channel_(buflo_channel)
    , sid_(-1)
    , handler_done_cb_(handler_done_cb)
    , state_(State::READ_SOCKS5_GREETING)
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
                                    bool ok,
                                    const in_addr_t& addr,
                                    const uint16_t& port) noexcept
{
    vlogself(2) << "begin";

    CHECK_EQ(state_, State::CREATE_BUFLO_STREAM);
    if (ok) {
        CHECK_GT(sid_, -1);

        _write_socks5_connect_request_granted();

        state_ = State::FORWARDING;

        // hand off the the two streams to inner outer handler to do
        // the forwarding
        inner_outer_handler_.reset(
            new InnerOuterHandler(
                client_channel_.get(), sid_, buflo_channel_,
                boost::bind(&ClientHandler::_on_inner_outer_handler_done,
                            this, _1, _2)));

        /* we need to hang on to the buflo_channel_ so that if the
         * inner outer handler tells us the outer stream has closed,
         * then we can close the inner stream
         */
        // buflo_channel_ = nullptr;
    } else {
        _close();
    }

    vlogself(2) << "done";
}

void
ClientHandler::_on_inner_outer_handler_done(InnerOuterHandler*,
                                            bool inner_stream_already_closed)
{
    CHECK_EQ(state_, State::FORWARDING);
    if (inner_stream_already_closed) {
        buflo_channel_ = nullptr; /* stream is closed, so clear
                                   * buflo_channel_ so we don't try to
                                   * tell it close the stream
                                   */
    }
    _close();
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
    buflo_channel_ = nullptr; // stream is closed, so clear
                              // buflo_channel_ so we don't try to
                              // tell it close the stream
    _close();
}


void
ClientHandler::onNewReadDataAvailable(StreamChannel* ch) noexcept
{
    // read data from client
    _consume_client_input();
}

void
ClientHandler::_write_socks5_connect_request_granted()
{
    // we can use this same resp whether the client requested to
    // connect to an ip address or a hostname: the last 6 bytes, i.e.,
    // the ip address and port, can be 0 --- we observe that ssh
    // sock5s proxy does this
    static const unsigned char resp[11] = "\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00";
    // 11 because of null-terminator
    static_assert(sizeof (resp) == 11, "resp ARRAY should be 11");

    auto rv = client_channel_->write(resp, 10);
    CHECK_EQ(rv, 0);
}

bool
ClientHandler::_read_socks5_greeting(size_t num_avail_bytes)
{
    if (num_avail_bytes >= 2) {
        vlogself(2) << "read socks5 greeting from client";

        const uint8_t *buf = client_channel_->peek(2);
        CHECK_NOTNULL(buf);

        CHECK_EQ(buf[0], '\x05');

        // how many authentication methods?
        const uint8_t nummethods = buf[1];

        vlogself(2) << "num auth methods client supports: " << unsigned(nummethods);

        const auto total_greeting_size = 2 + nummethods;
        CHECK_EQ(num_avail_bytes, total_greeting_size);

        buf = client_channel_->peek(total_greeting_size);
        CHECK_NOTNULL(buf);

        // we support only the "no-authentication" method
        bool found_no_auth_method = false;
        for (auto i = 0; i < nummethods; ++i) {
            auto const method = *(buf + 2 + i);
            if (method == 0) {
                found_no_auth_method = true;
                break;
            }
        }

        if (!found_no_auth_method) {
            logself(ERROR) << "we can only do the 'no-authentication' method";
            _close();
            return false;
        }

        // drain the inbuf
        auto rv = client_channel_->drain(total_greeting_size);
        CHECK_EQ(rv, 0);

        // valid greeting, so we write the greeting response
        static const unsigned char greeting_resp[] = "\x05\x00";
        rv = client_channel_->write(greeting_resp, 2);
        CHECK_EQ(rv, 0);
        state_ = State::READ_SOCKS5_CONNECT_REQ;
        vlogself(2) << "... good -> send reply greeting";
        return true;
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
    if (num_avail_bytes >= 4) {
        unsigned char *buf = client_channel_->peek(num_avail_bytes);
        CHECK_NOTNULL(buf);

        vlogself(2) << "read socks5 connect request from client";

        if (!memcmp(buf, "\x05\x01\x00\x01", 4)) {
            CHECK_EQ(num_avail_bytes, 10); // must be exactly 10
                                           // bytes, with the last 6
                                           // bytes being the addr and
                                           // port

            // read the addr and port
            in_addr_t target_addr;
            uint16_t port = 0;
            memcpy((uint8_t*)&target_addr, buf+4, 4);
            memcpy((uint8_t*)&port, buf+8, 2);
            port = ntohs(port);

            struct sockaddr_in sa;
            char str[INET_ADDRSTRLEN] = {0};
            sa.sin_addr.s_addr = target_addr;
            auto ret = inet_ntop(AF_INET, &(sa.sin_addr), str, INET_ADDRSTRLEN);
            CHECK_EQ(ret, str) << "inet_ntop failed; errno: " << errno;
            vlogself(2) << "connect req: [" << str << "]:" << port;

            auto rv = buflo_channel_->create_stream2(str, port, this);
            CHECK_EQ(rv, 0);

            // we can now drain the bytes
            rv = client_channel_->drain(10);
            CHECK_EQ(rv, 0);

            state_ = State::CREATE_BUFLO_STREAM;

        } else if (!memcmp(buf, "\x05\x01\x00\x03", 4)) {

            vlogself(2) << "client sent hostname";

            // field 5: 1 byte of name length followed by the name for domain name

            CHECK_GE(num_avail_bytes, 5);
            uint8_t namelen = 0;
            memcpy(&namelen, buf + 4, 1);

            vlogself(2) << "hostname len: " << unsigned(namelen);

            /* 4 header + 1 name len field + name + 2 for port */
            const auto total_req_size = (5 + namelen + 2);

            // + 2 for the port
            CHECK_EQ(num_avail_bytes, total_req_size);

            const string name((const char*)(buf + 5), namelen);
            vlogself(2) << "hostname: [" << name << "]";

            uint16_t port = 0;
            memcpy((uint8_t*)&port, buf + 5 + namelen, 2);
            port = ntohs(port);

            vlogself(2) << "port: [" << port << "]";

            auto rv = buflo_channel_->create_stream2(name.c_str(), port, this);
            CHECK_EQ(rv, 0);

            // we can now drain the bytes
            rv = client_channel_->drain(total_req_size);
            CHECK_EQ(rv, 0);

            state_ = State::CREATE_BUFLO_STREAM;
        } else {
            logself(WARNING) << "bad socks5 request; closing down";
            _close();
        }
    }

    // we should not consume more input because either we're now
    // waiting for stream creation or there's not enough data yet to
    // reach the socks5 request
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
    } while (keep_consuming && (state_ != State::CLOSED));

}

void
ClientHandler::onWrittenData(StreamChannel* ch) noexcept
{
    // don't care
}

void
ClientHandler::onEOF(StreamChannel*) noexcept
{
    vlogself(2) << "client channel eof";
    CHECK((state_ == State::READ_SOCKS5_CONNECT_REQ) ||
          (state_ == State::READ_SOCKS5_GREETING));
    _close();
}

void
ClientHandler::onError(StreamChannel*, int) noexcept
{
    vlogself(2) << "client channel error";
    CHECK((state_ == State::READ_SOCKS5_CONNECT_REQ) ||
          (state_ == State::READ_SOCKS5_GREETING));
    _close();
}

/* basically we don't support half-closed tunnels; we tear down when
 * either side closes. but we can reach close from several different
 * paths:
 *
 * * when we're still establishing the tunnel (e.g. while reading
 * socks5 request with client, or while creating the bufflo
 * stream). and if we close_stream() the buflo channel might call our
 * onStreamClosed() callback, etc.
 *
 * * we're notified that the innerouterhandler is done
 *
 * * the csp destroys us (e.g., due to its buflo mux channel with ssp
 * is torn down)
 *
 * so it's a little complex, our "free/close" calls can cause others
 * to call us. so we adopt this simple strategy: on any error/close
 * from anyone, we close by: first step is checking the barrier:
 * whether state is CLOSED, if not, set it, and then 1) reset/close
 * outer client channel, 2) reset/close inner stream if not null, 3)
 * notify csp we're done (calling handler_done_cb_) if not null
 */
void
ClientHandler::_close()
{
    /* avoid logging in this function because it can be called by
     * destructor and we're seeing valgrind report "invalid read" at
     * the last log statement
     */

    // vlogself(2) << "begin";

    if (state_ != State::CLOSED) {
        state_ = State::CLOSED;

        client_channel_.reset();

        if (buflo_channel_) {
            // unregistering ourselves as the stream observer so we won't
            // be called back at onStreamClosed()
            if (sid_ > -1) {
                buflo_channel_->set_stream_observer(sid_, nullptr);
                buflo_channel_->close_stream(sid_);
                buflo_channel_ = nullptr;
            }
        }

        if (handler_done_cb_) {
            DestructorGuard dg(this);
            handler_done_cb_(this);
            handler_done_cb_ = NULL;
        }
    }

    // vlogself(2) << "done";
}

ClientHandler::~ClientHandler()
{
    vlogself(2) << "clienthandler destructing";
    // currently we expect only the ClientSideProxy deletes us (e.g.,
    // client_handlers_.clear() when it tears down the buflo tunnel),
    // so we don't need to notify it (because it might free us again),
    // resulting in double free
    handler_done_cb_ = NULL;
    _close();
}

} // namespace csp
