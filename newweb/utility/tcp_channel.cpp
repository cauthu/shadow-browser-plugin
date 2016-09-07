

#include <event2/event.h>

#include "tcp_channel.hpp"
#include "easylogging++.h"
#include "common.hpp"


/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) << "tcpCh= " << (inst)->objId() << " "
#define vlogself(level) vloginst(level, this)

#define loginst(level, inst) LOG(level) << "tcpCh= " << (inst)->objId() << " "
#define logself(level) loginst(level, this)

namespace myio {

/*******************************************/

TCPChannel::TCPChannel(struct event_base *evbase,
                       const in_addr_t& addr, const in_port_t& port,
                       StreamChannelObserver* observer)
    : TCPChannel(evbase, -1, addr, port, observer, ChannelState::INIT, true)
{
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
    CHECK(!observer_);
    observer_ = observer;
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
    state_ = ChannelState::CLOSED;
    socket_read_ev_.reset();
    socket_write_ev_.reset();
    input_evb_.reset(); // XXX/maybe we can keep the input buf for
                        // client to read
    output_evb_.reset();
    if (fd_) {
        ::close(fd_);
    }
    tamaraw_timer_ev_.reset();
}

bool
TCPChannel::is_closed() const
{
    return state_ == ChannelState::CLOSED;
}

void
TCPChannel::drop_future_input(StreamChannelInputDropObserver* observer,
                              size_t len)
{
    // crash if we have not fully completed the previous request
    CHECK(!input_drop_req_.observer);
    CHECK_EQ(input_drop_req_.num_requested, 0);
    CHECK_EQ(input_drop_req_.num_remaining, 0);

    CHECK_NOTNULL(observer);
    CHECK_GT(len, 0);

    input_drop_req_.observer = observer;
    input_drop_req_.num_requested = input_drop_req_.num_remaining = len;
}

int
TCPChannel::write_dummy(size_t len)
{
    vlogself(2) << "begin, len: " << len;
    while (len > 0) {
        /* add reference to static bytes so we don't need to copy. we
         * don't need to pass a free callback.
         */
        const auto num_to_add = std::min(len, common::static_bytes.length());
        const auto rv = evbuffer_add_reference(
            output_evb_.get(), common::static_bytes.c_str(),
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
    vlogself(2) << "initialize (but not enable) read and write events, fd= " << fd_;
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
    CHECK_EQ(state_, ChannelState::SOCKET_CONNECTED);
    if (force_enable || evbuffer_get_length(output_evb_.get())) {
        // there is some output data to write, or we are being forced,
        // then start monitoring
        auto rv = event_add(socket_write_ev_.get(), nullptr);
        CHECK_EQ(rv, 0);
    } else {
        // stop monitoring
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
            const auto rv = evbuffer_read(input_evb_.get(), fd_, -1);
            vlogself(2) << "evbuffer_read() returns: " << rv;
            if (rv > 0 && evbuffer_get_length(input_evb_.get()) >= read_lw_mark_) {
                observer_->onNewReadDataAvailable(this);
            }
            if (rv == 0) {
                observer_->onEOF(this);
            } else if ((rv == -1) && (errno != EAGAIN)) {
                observer_->onError(this, errno);
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
    // will possibly notify observer, so but not going to set up a
    // destructor guard here; leave it caller to do

    // don't care about contents, so can use static
    static char drop_buf[4096];
    auto maybe_theres_more = true;

    while (input_drop_req_.num_remaining > 0) {
        // read into drop buf instead of into input_evb_
        vlogself(2) << "want to dropread " << input_drop_req_.num_remaining << " bytes";
        const auto num_to_read =
            std::min(input_drop_req_.num_remaining, sizeof drop_buf);
        const auto rv = ::read(fd_, drop_buf, num_to_read);
        vlogself(2) << "got " << rv;
        if (rv > 0) {
            input_drop_req_.num_remaining -= rv;
            if (input_drop_req_.num_remaining == 0) {
                // notify first before resetting, to prevent user from
                // immediately submitting another drop req. not that
                // we couldn't handle it; just dont think we need to
                input_drop_req_.observer->onCompleteInputDrop(
                    this, input_drop_req_.num_requested);
                // now reset
                input_drop_req_.observer = nullptr;
                input_drop_req_.num_requested = 0;
            }
        } else {
            if (rv == 0) {
                observer_->onEOF(this);
            } else if ((rv == -1) && (errno != EAGAIN)) {
                observer_->onError(this, errno);
            }
            maybe_theres_more = false;
            break;
        }
    }

    vlogself(2) << "done, returning: " << maybe_theres_more;
    return maybe_theres_more;
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
        if (rv == 0) {
            _on_eof();
        } else if (rv == -1) {
            if (errno == EINPROGRESS) {
                logself(FATAL) << "getting EINPROGRESS after a write()";
            } else if (errno != EAGAIN) {
                logself(WARNING) << "errno= " << errno << " (" << strerror(errno) << ")";
                _on_error();
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
    observer_->onEOF(this);
}

void
TCPChannel::_on_error()
{
    // assuming desctructorguard already set up
    close();
    observer_->onError(this, errno);
}

TCPChannel::TCPChannel(struct event_base *evbase, int fd,
                       const in_addr_t& addr, const in_port_t& port,
                       StreamChannelObserver* observer,
                       ChannelState starting_state, bool is_client)
    : evbase_(evbase), observer_(nullptr), connect_observer_(nullptr)
    , state_(starting_state), fd_(fd)
    , socket_read_ev_(nullptr, event_free)
    , socket_write_ev_(nullptr, event_free)
    , addr_(addr), port_(port), is_client_(is_client)
    , input_evb_(evbuffer_new(), evbuffer_free)
    , output_evb_(evbuffer_new(), evbuffer_free)
    , read_lw_mark_(0)
    , tamaraw_timer_ev_(nullptr, event_free)
{
    input_drop_req_.observer = nullptr;
    input_drop_req_.num_requested = input_drop_req_.num_remaining = 0;
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

} // end myio namespace
