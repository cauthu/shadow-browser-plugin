

#include <arpa/inet.h>
#include <sys/socket.h>
#include <event2/event.h>
#include <algorithm>
#include <boost/bind.hpp>

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
    DCHECK(input_evb_);
    return evbuffer_remove(input_evb_.get(), data, len);
}

int
TCPChannel::read_buffer(struct evbuffer* buf, size_t len)
{
    DCHECK(input_evb_);
    return evbuffer_remove_buffer(input_evb_.get(), buf, len);
}

int
TCPChannel::drain(size_t len)
{
    DCHECK(input_evb_);
    return evbuffer_drain(input_evb_.get(), len);
}

uint8_t*
TCPChannel::peek(ssize_t len)
{
    DCHECK(input_evb_);
    return evbuffer_pullup(input_evb_.get(), len);
}

size_t
TCPChannel::get_avail_input_length() const
{
    DCHECK(input_evb_);
    return evbuffer_get_length(input_evb_.get());
}

size_t
TCPChannel::get_output_length() const
{
    DCHECK(output_evb_);
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
    DCHECK(output_evb_);
    const auto rv = evbuffer_add(output_evb_.get(), data, size);
    if (!rv) {
        _maybe_toggle_write_monitoring(true);
    }
    return rv;
}

int
TCPChannel::write_buffer(struct evbuffer* buf)
{
    DCHECK(output_evb_);
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
        state_ = ChannelState::SOCKET_CONNECTED;
        _set_read_monitoring(true);
        _maybe_toggle_write_monitoring();
        connect_observer_->onConnected(this);
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
        vlogself(3) << "start monitoring read event for fd= " << fd_;
        auto rv = event_add(socket_read_ev_.get(), nullptr);
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

    if (what & EV_READ) {
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
        CHECK(0) << "invalid events: " << what;
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

    while (input_drop_.num_remaining() && maybe_theres_more) {
        // read into drop buf instead of into input_evb_
        dvlogself(3) << "want to dropread "
                     << input_drop_.num_remaining() << " bytes";
        const auto num_to_read =
            std::min(input_drop_.num_remaining(), sizeof drop_buf);
        const auto rv = ::read(fd_, drop_buf, num_to_read);
        dvlogself(3) << "got " << rv;
        if (rv > 0) {
            num_total_read_bytes_ += rv;
            dropped_this_time += rv;
            input_drop_.progress(rv);
        } else {
            _handle_non_successful_socket_io("dropread", rv, true);

            // whetever the reason, there is no more to read this time
            // around
            maybe_theres_more = false;
        }
    }

    if (dropped_this_time) {
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

    vlogself(3) << "done, returning: " << maybe_theres_more;
    return maybe_theres_more;
}

void
TCPChannel::_handle_non_successful_socket_io(const char* io_op_str,
                                             const ssize_t rv,
                                             const bool crash_if_EINPROGRESS)
{
    if (rv == 0) {
        _on_eof();
    } else {
        DCHECK_EQ(rv, -1);
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
    // assuming desctructorguard already set up
    close();
    DCHECK_NOTNULL(observer_);
    observer_->onEOF(this);
}

void
TCPChannel::_on_error()
{
    // assuming desctructorguard already set up
    close();
    DCHECK_NOTNULL(observer_);
    observer_->onError(this, errno);
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
{
    DCHECK_EQ(observer_, observer);
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
    vlogself(2) << "tcpchannel destructing (fd= " << fd_ << ")";
    close();
}

} // end myio namespace
