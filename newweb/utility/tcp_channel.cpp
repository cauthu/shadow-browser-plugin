

#include <event2/event.h>

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
    // observer can be set later with set_observer()
    CHECK(is_client_);
}

TCPChannel::TCPChannel(struct event_base *evbase, int fd)
    : TCPChannel(evbase, fd, 0, 0, nullptr, ChannelState::SOCKET_CONNECTED, false)
{
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
    CHECK_EQ(errno, EINPROGRESS);

    rv = event_base_once(
        evbase_, fd_, EV_WRITE, s_socket_connect_eventcb,
        this, connect_timeout);
    CHECK_EQ(rv, 0);

    state_ = ChannelState::CONNECTING_SOCKET;

    _initialize_read_write_events();

    return 0;
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
    CHECK_EQ(evbuffer_get_length(input_evb_.get()), 0);

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
    vlogself(2) << "begin, len: " << len;
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
    vlogself(2) << "done";
    return 0;
}

/********************/

void
TCPChannel::_initialize_read_write_events()
{
    CHECK_GE(fd_, 0);
    // vlogself(2) << "initialize (but not enable) read and write events, fd= " << fd_;
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

    if (what & EV_WRITE) {
        state_ = ChannelState::SOCKET_CONNECTED;
        _set_read_monitoring(true);
        _maybe_toggle_write_monitoring();
        connect_observer_->onConnected(this);
        // vlogself(2) << " current observer: " << observer_;
    }
    else if (what & EV_TIMEOUT) {
        close();
        connect_observer_->onConnectTimeout(this);
    }
    else {
        CHECK(0) << "unexpected events: " << what;
    }
}

void
TCPChannel::_set_read_monitoring(bool enabled)
{
    CHECK_EQ(state_, ChannelState::SOCKET_CONNECTED);
    if (enabled) {
        vlogself(2) << "start monitoring read event for fd= " << fd_;
        auto rv = event_add(socket_read_ev_.get(), nullptr);
        CHECK_EQ(rv, 0);
    } else {
        vlogself(2) << "STOP monitoring read event for fd= " << fd_;
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
        dvlogself(2) << "stop monitoring";
        auto rv = event_del(socket_write_ev_.get());
        CHECK_EQ(rv, 0);
    }
}

void
TCPChannel::_on_socket_readcb(int fd, short what)
{
    CHECK_EQ(fd, fd_);

    vlogself(2) << "begin";

    DestructorGuard dg(this);

    if (what & EV_READ) {
        if (_maybe_dropread()) {
            // should NOT try to empty the socket's read buffer (e.g.,
            // by looping and reading until EAGAIN) because the user
            // might need just a few bytes and decide to issue a drop
            // request
            const auto rv = evbuffer_read(
                input_evb_.get(), fd_, read_size_hint_);
            vlogself(2) << "evbuffer_read() returns: " << rv;
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

    vlogself(2) << "done";
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
        dvlogself(2) << "want to dropread "
                     << input_drop_.num_remaining() << " bytes";
        const auto num_to_read =
            std::min(input_drop_.num_remaining(), sizeof drop_buf);
        const auto rv = ::read(fd_, drop_buf, num_to_read);
        dvlogself(2) << "got " << rv;
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
        vlogself(2) << dropped_this_time << " bytes dropped this time around"
                    << " (" << input_drop_.num_remaining() << " remaining)";
        if (input_drop_.interested_in_progress()) {
            // notify of any new progress
            input_drop_.observer()->onInputBytesDropped(this, dropped_this_time);
        } else if (input_drop_.num_remaining()) {
            // notify just once, when have dropped all requested amount

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

    vlogself(2) << "done, returning: " << maybe_theres_more;
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
            logself(WARNING) << io_op_str << " got errno= " << errno
                             << " (" << strerror(errno) << ")";
            _on_error();
        }
    }
}

void
TCPChannel::_on_socket_writecb(int fd, short what)
{
    CHECK_EQ(fd, fd_);

    vlogself(2) << "begin";

    DestructorGuard dg(this);

    if (what & EV_WRITE) {
        const auto rv = evbuffer_write(output_evb_.get(), fd_);
        vlogself(2) << "evbuffer_write() return: " << rv;
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

    vlogself(2) << "done";
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
