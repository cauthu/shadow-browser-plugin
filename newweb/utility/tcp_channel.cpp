

#include <arpa/inet.h>
#include <sys/socket.h>
#include <event2/event.h>
#include <algorithm>
#include <boost/bind.hpp>

#include <sys/types.h>
#include <sys/socket.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "tcp_channel.hpp"
#include "easylogging++.h"
#include "common.hpp"


#define _LOG_PREFIX(inst) << "tcpCh= " << (inst)->objId() << " (fd=" << fd_ << "): "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


namespace myio {

/*******************************************/

TCPChannel::TCPChannel(struct event_base *evbase,
                       const in_addr_t& addr, const in_port_t& port,
                       StreamChannelObserver* observer)
    : TCPChannel(evbase, -1, addr, port, observer, ChannelState::INIT, true)
{
    connect_errno_ = 0;
    // observer can be set later with set_observer()
    CHECK(is_client_);
}

TCPChannel::TCPChannel(struct event_base *evbase, int fd)
    : TCPChannel(evbase, fd, 0, 0, nullptr, ChannelState::SOCKET_CONNECTED, false)
{
    connect_errno_ = 0;
    CHECK_GE(fd_, 0);
    CHECK(!is_client_);
    vlogself(2) << "contructing a (server) tcp chan, fd= " << fd;
    _initialize_read_write_events();
    _set_read_monitoring(true);
    _maybe_toggle_write_monitoring();
}

int
TCPChannel::start_connecting(StreamChannelConnectObserver* observer,
                             struct timeval *connect_timeout)
{
    CHECK_EQ(state_, ChannelState::INIT);
    CHECK(is_client_);

    vlogself(2) << "start connecting...";

    connect_observer_ = observer;
    CHECK_NOTNULL(connect_observer_);

    fd_ = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
    CHECK_NE(fd_, -1);

    struct sockaddr_in server;
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = addr_;
    server.sin_port = htons(port_);

    auto rv = connect(fd_, (struct sockaddr*)&server, sizeof(server));
    CHECK_EQ(rv, -1);
    connect_errno_ = errno;

    if (connect_errno_ != EINPROGRESS) {
        vlogself(1) << "errno: " << errno;

        state_ = ChannelState::CONNECTING_SOCKET;

        /* call the onConnectError() in a separate stack */
        connect_error_cb_timer_.reset(
            new Timer(evbase_, true,
                      boost::bind(&TCPChannel::_on_socket_connect_errorcb, this, _1)));
        CHECK_NOTNULL(connect_error_cb_timer_.get());

        static const auto zero_ms = 0;
        connect_error_cb_timer_->start(zero_ms);
        CHECK(connect_error_cb_timer_->is_running());
    }
    else {
        state_ = ChannelState::CONNECTING_SOCKET;

        socket_connect_ev_.reset(
            event_new(evbase_, fd_, EV_READ | EV_WRITE | EV_TIMEOUT,
                      s_socket_connect_eventcb, this));
        CHECK_NOTNULL(socket_connect_ev_.get());

        rv = event_add(socket_connect_ev_.get(), connect_timeout);
        CHECK_EQ(rv, 0);

        _initialize_read_write_events();
    }

    return 0;
}

#ifndef IN_SHADOW
int
TCPChannel::start_ssl(SSL_CTX* ssl_ctx)
{
    vlogself(2) << "begin, ssl_ctx= " << ssl_ctx;

    CHECK(state_ == ChannelState::SOCKET_CONNECTED);
    CHECK(ssl_ctx_ == nullptr);

    ssl_ctx_ = ssl_ctx;
    CHECK_NOTNULL(ssl_ctx_);

    state_ = ChannelState::SSL_HANDSHAKING;

    ssl_.reset(SSL_new(ssl_ctx_));
    auto rv = SSL_set_fd(ssl_.get(), fd_);
    CHECK(rv == 1) << "rv= " << rv;

    if (is_client_) {
        SSL_set_connect_state(ssl_.get());
    } else {
        SSL_set_accept_state(ssl_.get());
    }

    rv = SSL_do_handshake(ssl_.get());
    CHECK(rv == -1) << "rv= " << rv;
    const auto ssl_err = SSL_get_error(ssl_.get(), rv);

    vlogself(2) << "ssl_err= " << ssl_err;
    _maybe_enable_socket_io_for_ssl(ssl_err);

    vlogself(2) << "done";

    return 0;
}

void
TCPChannel::_maybe_enable_socket_io_for_ssl(const int& ssl_err)
{
    vlogself(2) << "begin, err= " << ssl_err;
    switch (ssl_err) {
    case SSL_ERROR_WANT_READ:
        vlogself(2) << "ssl wants read";
        _set_read_monitoring(true);
        // no "break" here, to fall through to enable write if
        // necessary
    case SSL_ERROR_WANT_WRITE:
        vlogself(2) << "ssl wants write";
        _maybe_toggle_write_monitoring(true);
        break;
    default:
        CHECK(false);
        break;
    }
    vlogself(2) << "done";
}

void
TCPChannel::_do_ssl_read()
{
    // char buf[1024];
    // const auto rv = SSL_read(ssl_.get(), buf, sizeof buf);

    // CHECK(false) << rv;

    if (state_ == ChannelState::SSL_HANDSHAKING) {
        auto rv = SSL_do_handshake(ssl_.get());
        vlogself(2) << "rv= " << rv;
        if (rv == 1) {

        } else if (rv == 2) {

        } else {
            const auto ssl_err = SSL_get_error(ssl_.get(), rv);

            vlogself(2) << "ssl_err= " << ssl_err;
            _maybe_enable_socket_io_for_ssl(ssl_err);
        }
    }
}

void
TCPChannel::_do_ssl_write()
{
    return;
    vlogself(2) << "begin";

    if (state_ == ChannelState::SSL_HANDSHAKING) {
        auto rv = SSL_do_handshake(ssl_.get());
        const auto ssl_err = SSL_get_error(ssl_.get(), rv);

        vlogself(2) << "ssl_err= " << ssl_err;
        _maybe_enable_socket_io_for_ssl(ssl_err);
    }
    
    // struct evbuffer_iovec *v;
    // size_t written = 0;
    // int n, i, r;

    // /* determine how many chunks we need. */
    // n = evbuffer_peek(buf, 4096, NULL, NULL, 0);
    // /* Allocate space for the chunks.  This would be a good time to use
    //    alloca() if you have it. */
    // v = malloc(sizeof(struct evbuffer_iovec)*n);
    // /* Actually fill up v. */
    // n = evbuffer_peek(buf, 4096, NULL, v, n);
    // for (i=0; i<n; ++i) {
    //     size_t len = v[i].iov_len;
    //     if (written + len > 4096)
    //         len = 4096 - written;
    //     r = write(1 /* stdout */, v[i].iov_base, len);
    //     if (r<=0)
    //         break;
    //     /* We keep track of the bytes written separately; if we don't,
    //        we may write more than 4096 bytes if the last chunk puts
    //        us over the limit. */
    //     written += len;
    // }
    // free(v);
    
    // const auto rv = SSL_read(ssl_.get(), buf, sizeof buf);

    // CHECK(false) << rv;

    vlogself(2) << "done";
}

#endif

void
TCPChannel::get_peer_name(std::string& address, uint16_t& port) const
{
    CHECK_EQ(state_, ChannelState::SOCKET_CONNECTED);

    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    char ipstr[INET6_ADDRSTRLEN] = {0};

    auto rv = getpeername(fd_, (struct sockaddr*)&addr, &len);
    CHECK_EQ(rv, 0);

    // deal with both IPv4 and IPv6:
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);

        address = ipstr;
    } else { // AF_INET6
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
        port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);

        address = ipstr;
    }
}

void
TCPChannel::set_observer(StreamChannelObserver* observer)
{
    CHECK_EQ(state_, ChannelState::SOCKET_CONNECTED);

    StreamChannel::set_observer(observer);

    // might need better way to handle the case when input buffer is
    // not empty and we have a new observer, because currently, the
    // new observer won't be notified of onNewReadDataAvailable()
    // until next time new data comes in from socket, which might not
    // happen
    CHECK_EQ(get_avail_input_length(), 0);
}

int
TCPChannel::read(uint8_t *data, size_t len)
{
    CHECK(input_evb_);
    return evbuffer_remove(input_evb_.get(), data, len);
}

int
TCPChannel::read_buffer(struct evbuffer* buf, size_t len)
{
    CHECK(input_evb_);
    return evbuffer_remove_buffer(input_evb_.get(), buf, len);
}

int
TCPChannel::drain(size_t len)
{
    CHECK(input_evb_);
    return evbuffer_drain(input_evb_.get(), len);
}

uint8_t*
TCPChannel::peek(ssize_t len)
{
    CHECK(input_evb_);
    return evbuffer_pullup(input_evb_.get(), len);
}

size_t
TCPChannel::get_avail_input_length() const
{
    CHECK(input_evb_);
    return evbuffer_get_length(input_evb_.get());
}

size_t
TCPChannel::get_output_length() const
{
    CHECK(output_evb_);
    return evbuffer_get_length(output_evb_.get());
}

void
TCPChannel::set_read_watermark(size_t lowmark, size_t highmark)
{
    CHECK_LE(lowmark, 0xffff);
    read_lw_mark_ = lowmark;
    CHECK_EQ(highmark, 0);
}

int
TCPChannel::write(const uint8_t *data, size_t size)
{
    CHECK(output_evb_);
    const auto rv = evbuffer_add(output_evb_.get(), data, size);
    if (!rv) {
        _maybe_toggle_write_monitoring(true);
    }
    return rv;
}

int
TCPChannel::write_buffer(struct evbuffer* buf)
{
    CHECK(output_evb_) _LOG_PREFIX(this);
    const auto rv = evbuffer_add_buffer(output_evb_.get(), buf);
    if (!rv) {
        _maybe_toggle_write_monitoring(true);
    }
    return rv;
}

void
TCPChannel::close()
{
    if (state_ == ChannelState::CLOSED) {
        return;
    }

    state_ = ChannelState::CLOSED;
    connect_error_cb_timer_.reset();
    socket_connect_ev_.reset();
    socket_read_ev_.reset();
    socket_write_ev_.reset();
    input_evb_.reset(); // XXX/maybe we can keep the input buf for
                        // client to read
    output_evb_.reset();
    if (fd_ != -1) {
        vlogself(2) << "::close(fd=" << fd_ << ")";
        ::close(fd_);
        fd_ = -1;
    }
}

bool
TCPChannel::is_closed() const
{
    return state_ == ChannelState::CLOSED;
}

int
TCPChannel::release_fd()
{
    // if there's still data in the input buffer that the user has not
    // read, then it's a bug
    const auto remaining_input_amt = evbuffer_get_length(input_evb_.get());
    if (remaining_input_amt) {
        const auto amnt_to_log = std::min(remaining_input_amt, static_cast<unsigned long>(10));
        auto bytes = evbuffer_pullup(input_evb_.get(), amnt_to_log);
        CHECK_NOTNULL(bytes);
        // it takes 2 hex chars to represent one byte
        char *hex = (char*)calloc(1, (amnt_to_log*2) + 1);
        CHECK_NOTNULL(hex);
        common::to_hex(bytes, amnt_to_log, hex);
        logself(FATAL) << "there are still " << remaining_input_amt
                       << " bytes in input buffer; here is hex of first "
                       << amnt_to_log << " bytes: [" << hex << "]";
        free(hex);
    }

    auto ret = fd_;
    fd_ = -1;
    close();
    return ret;
}

void
TCPChannel::drop_future_input(StreamChannelInputDropObserver* observer,
                              size_t len, bool notify_progress)
{
    // crash if we have not fully completed the previous request
    CHECK(!input_drop_.is_active());
    input_drop_.set(observer, len, notify_progress);
}

int
TCPChannel::write_dummy(size_t len)
{
    vlogself(3) << "begin, len: " << len;
    while (len > 0) {
        /* add reference to static bytes so we don't need to copy. we
         * don't need to pass a free callback.
         */
        const auto num_to_add = std::min(len, common::static_bytes_length);
        const auto rv = evbuffer_add_reference(
            output_evb_.get(), common::static_bytes->c_str(),
            num_to_add, nullptr, nullptr);
        CHECK_EQ(rv, 0);
        len -= num_to_add;
    }
    vlogself(3) << "done";
    return 0;
}

/********************/

void
TCPChannel::_initialize_read_write_events()
{
    CHECK_GE(fd_, 0);
    // vlogself(3) << "initialize (but not enable) read and write events, fd= " << fd_;
    socket_read_ev_.reset(
        event_new(evbase_, fd_, EV_READ | EV_PERSIST, s_socket_readcb, this));
    socket_write_ev_.reset(
        event_new(evbase_, fd_, EV_WRITE | EV_PERSIST, s_socket_writecb, this));
}

void
TCPChannel::_on_socket_connect_eventcb(int fd, short what)
{
    CHECK_EQ(fd, fd_);
    CHECK_EQ(state_, ChannelState::CONNECTING_SOCKET);

    DestructorGuard dg(this);

    const auto rv = event_del(socket_connect_ev_.get());
    CHECK_EQ(rv, 0);

    if ((what & EV_WRITE) || (what & EV_READ)) {
        int sockerror = 0;
        unsigned int optlen = sizeof sockerror;

        const auto rv = getsockopt(fd_, SOL_SOCKET, SO_ERROR, &sockerror, &optlen);
        if (!rv && !sockerror) {
            state_ = ChannelState::SOCKET_CONNECTED;
            _set_read_monitoring(true);
            _maybe_toggle_write_monitoring();
            connect_observer_->onConnected(this);
        } else {
            vlogself(1) << "getsockopt() rv= " << rv
                        << " sockerror= " << sockerror;
            close();
            connect_observer_->onConnectError(this, sockerror);
        }
    }
    else if (what & EV_TIMEOUT) {
        close();
        connect_observer_->onConnectTimeout(this);
    }
    else {
        logself(FATAL) << "unexpected events: " << unsigned(what);
    }
}

void
TCPChannel::_on_socket_connect_errorcb(Timer*)
{
    CHECK_EQ(state_, ChannelState::CONNECTING_SOCKET);
    CHECK_NE(connect_errno_, 0);

    DestructorGuard dg(this);
    connect_observer_->onConnectError(this, connect_errno_);
}

void
TCPChannel::_set_read_monitoring(bool enabled)
{
    CHECK_EQ(state_, ChannelState::SOCKET_CONNECTED);
    if (enabled) {

        /* poll to check for socket close
         *
         * we want to use EV_WRITE to be notified that socket is
         * closed... but shadow doesn't support edge-triggered event,
         * so we will repeatedly get the EV_WRITE event for an idle
         * socket. so we can't use it; same reason we have to use
         * _maybe_toggle_write_monitoring() -- lack of edge-triggered
         * event support. so we use a polling method: set a time out
         * on the read event, and we try to read on timeout, and it
         * should return no bytes
         */
        struct timeval timeout_tv;
        timeout_tv.tv_sec = 5;
        timeout_tv.tv_usec = 0;

        vlogself(3) << "start monitoring read event for fd= " << fd_;
        auto rv = event_add(socket_read_ev_.get(), &timeout_tv);
        CHECK_EQ(rv, 0);
    } else {
        vlogself(3) << "STOP monitoring read event for fd= " << fd_;
        auto rv = event_del(socket_read_ev_.get());
        CHECK_EQ(rv, 0);
    }
}

void
TCPChannel::_maybe_toggle_write_monitoring(bool force_enable)
{
    if (state_ != ChannelState::SOCKET_CONNECTED) {
        // we will toggle when socket is connected
        return;
    }
    if (force_enable || evbuffer_get_length(output_evb_.get())) {
        // there is some output data to write, or we are being forced,
        // then start monitoring
        auto rv = event_add(socket_write_ev_.get(), nullptr);
        CHECK_EQ(rv, 0);
    } else {
        dvlogself(3) << "stop monitoring";
        auto rv = event_del(socket_write_ev_.get());
        CHECK_EQ(rv, 0);
    }
}

void
TCPChannel::_on_socket_readcb(int fd, short what)
{
    CHECK_EQ(fd, fd_);

    vlogself(3) << "begin";

    DestructorGuard dg(this);

    if (what & (EV_READ | EV_TIMEOUT)) {
        if (_maybe_dropread()) {
            // should NOT try to empty the socket's read buffer (e.g.,
            // by looping and reading until EAGAIN) because the user
            // might need just a few bytes and decide to issue a drop
            // request
            const auto rv = evbuffer_read(
                input_evb_.get(), fd_, read_size_hint_);
            vlogself(3) << "evbuffer_read() returns: " << rv;
            if (rv > 0) {
                num_total_read_bytes_ += rv;
                if (evbuffer_get_length(input_evb_.get()) >= read_lw_mark_) {
                    CHECK_NOTNULL(observer_);
                    observer_->onNewReadDataAvailable(this);
                }
            } else {
                _handle_non_successful_socket_io("read", rv, true);
            }
        }
    } else {
        CHECK(0) << "invalid events: " << unsigned(what);
    }

    vlogself(3) << "done";
}

bool
TCPChannel::_maybe_dropread()
{
    // will possibly notify observer, but not going to set up a
    // destructor guard here; leave it caller to do

    // don't care about contents, so can use static
    static char drop_buf[4096]; /* though shadow's CONFIG_TCP_RMEM_MAX
                                 * is 6291456, it always seems to
                                 * allow max read of 4k at a time, so
                                 * we'll just go with 4k
                                 */
    auto maybe_theres_more = true; // from socket
    size_t dropped_this_time = 0; // to be accumulated in the loop,
                                  // and notify observer once before
                                  // we exit

    bool had_an_unsuccessful_read = false;
    int read_error = 0;

    while (input_drop_.num_remaining() && maybe_theres_more) {
        // read into drop buf instead of into input_evb_
        vlogself(3) << "want to dropread "
                     << input_drop_.num_remaining() << " bytes";
        const auto num_to_read =
            std::min(input_drop_.num_remaining(), sizeof drop_buf);
        const auto rv = ::read(fd_, drop_buf, num_to_read);
        vlogself(3) << "got " << rv;
        if (rv > 0) {
            num_total_read_bytes_ += rv;
            dropped_this_time += rv;
            input_drop_.progress(rv);

            if (rv < num_to_read) {
                // there was less data than we wanted, so the socket
                // doesn't have more for us at this time, so we can
                // break.
                //
                // this fixes a bug where we dont break and try to
                // read again in the loop and get zero, and we call
                // _handle_non_successful_socket_io() which notifies
                // the user of eof, and user destroys himself, and
                // then we proceed below to the next block with a
                // positive dropped_this_time and notifies a
                // now-deleted object

                maybe_theres_more = false;
            }
        } else {
            had_an_unsuccessful_read = true;
            read_error = rv;
            // whetever the reason, there is no more to read this time
            // around
            maybe_theres_more = false;
        }
    }

    if (dropped_this_time) {
        CHECK_NE(fd_, -1);
        CHECK_NE(state_, ChannelState::CLOSED);
        const auto remaining = input_drop_.num_remaining();
        vlogself(3) << dropped_this_time << " bytes dropped this time around"
                    << " (" << remaining << " remaining)";
        if (input_drop_.interested_in_progress()) {
            vlogself(3) << "notify of any new progress";
            input_drop_.observer()->onInputBytesDropped(this, dropped_this_time);

            if (remaining == 0) {
                // also have to reset here
                input_drop_.reset();
            }
        } else if (remaining == 0) {
            // notify just once, when have dropped all requested amount
            vlogself(3) << "notify once since we're done";

            /* notify first before resetting, to prevent user from
             * immediately submitting another drop req. not that we
             * couldn't handle it; just that it might indicate a bug
             */
            input_drop_.observer()->onInputBytesDropped(
                this, input_drop_.num_requested());

            // now reset
            input_drop_.reset();
        }
    }

    if (had_an_unsuccessful_read) {
        _handle_non_successful_socket_io("dropread", read_error, true);
    }

    vlogself(3) << "done, returning: " << maybe_theres_more;
    return maybe_theres_more;
}

void
TCPChannel::_handle_non_successful_socket_io(const char* io_op_str,
                                             const ssize_t rv,
                                             const bool crash_if_EINPROGRESS)
{
    vlogself(3) << "begin, io_op_str: " << io_op_str
                << ", error: " << strerror(errno);

    if (rv == 0) {
        _on_eof();
    } else {
        CHECK_EQ(rv, -1);
        if (errno == EAGAIN) {
            // can safely ingore
        } else if (errno == EINPROGRESS) {
            if (crash_if_EINPROGRESS) {
                logself(FATAL) << "getting EINPROGRESS after a " << io_op_str;
            }
        } else {
            // let's not war if the socket had been closed by peer
            if (errno == ECONNRESET) {
                vlogself(2) << io_op_str << " got errno= " << errno
                            << " (" << strerror(errno) << ")";
            } else {
                logself(WARNING) << io_op_str << " got errno= " << errno
                                 << " (" << strerror(errno) << ")";
            }
            _on_error();
        }
    }

    vlogself(3) << "done";
}

void
TCPChannel::_on_socket_writecb(int fd, short what)
{
    CHECK_EQ(fd, fd_);

    vlogself(3) << "begin";

    DestructorGuard dg(this);

    if (what & EV_WRITE) {
        const auto rv = evbuffer_write(output_evb_.get(), fd_);
        vlogself(3) << "evbuffer_write() return: " << rv;
        _maybe_toggle_write_monitoring();
        if (rv <= 0) {
            _handle_non_successful_socket_io("write", rv, true);
            CHECK_NOTNULL(observer_);
        } else {
            num_total_written_bytes_ += rv;
            static const size_t write_lw_mark_ = 0;
            if (evbuffer_get_length(output_evb_.get()) <= write_lw_mark_) {
                CHECK_NOTNULL(observer_);
                observer_->onWrittenData(this);
            }
        }
    } else {
        CHECK(0) << "invalid events: " << what;
    }

    vlogself(3) << "done";
}

void
TCPChannel::_on_eof()
{
    vlogself(2) << "begin";
    // assuming desctructorguard already set up
#ifndef IN_SHADOW
    close();
#endif

    CHECK_NOTNULL(observer_);
    observer_->onEOF(this);
    vlogself(2) << "done";
}

void
TCPChannel::_on_error()
{
    vlogself(2) << "begin";
    // assuming desctructorguard already set up
#ifndef IN_SHADOW
    close();
#endif

    CHECK_NOTNULL(observer_);
    observer_->onError(this, errno);
    vlogself(2) << "done";
}

TCPChannel::TCPChannel(struct event_base *evbase, int fd,
                       const in_addr_t& addr, const in_port_t& port,
                       StreamChannelObserver* observer,
                       ChannelState starting_state, bool is_client)
    : StreamChannel(observer)
    , evbase_(evbase), connect_observer_(nullptr)
    , state_(starting_state), fd_(fd)
    , socket_connect_ev_(nullptr, event_free)
    , socket_read_ev_(nullptr, event_free)
    , socket_write_ev_(nullptr, event_free)
    , addr_(addr), port_(port), is_client_(is_client)
    , input_evb_(evbuffer_new(), evbuffer_free)
    , output_evb_(evbuffer_new(), evbuffer_free)
    , read_lw_mark_(0)
#ifndef IN_SHADOW
    , ssl_(nullptr, SSL_free)
#endif
{
    CHECK_EQ(observer_, observer);
    input_drop_.reset();
}

void
TCPChannel::s_socket_connect_eventcb(int fd, short what, void* arg)
{
    TCPChannel* ch = (TCPChannel*)arg;
    ch->_on_socket_connect_eventcb(fd, what);
}

void
TCPChannel::s_socket_readcb(int fd, short what, void* arg)
{
    TCPChannel* ch = (TCPChannel*)arg;
    ch->_on_socket_readcb(fd, what);
}

void
TCPChannel::s_socket_writecb(int fd, short what, void* arg)
{
    TCPChannel* ch = (TCPChannel*)arg;
    ch->_on_socket_writecb(fd, what);
}

TCPChannel::~TCPChannel()
{
    vlogself(2) << "tcpchannel begin destructing (fd= " << fd_ << ")";
    close();
    vlogself(2) << "tcpchannel done destructing (fd= " << fd_ << ")";
}

} // end myio namespace
