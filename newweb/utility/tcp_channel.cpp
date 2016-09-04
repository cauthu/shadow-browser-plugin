

#include <event2/event.h>

#include "tcp_channel.hpp"
#include "easylogging++.h"

namespace myio {

/*******************************************/

TCPChannel::TCPChannel(struct event_base *evbase,
                       const in_addr_t& addr, const in_port_t& port,
                       StreamChannelObserver* observer)
    : TCPChannel(evbase, -1, addr, port, observer, ChannelState::INIT, true)
{}

TCPChannel::TCPChannel(struct event_base *evbase, int fd)
    : TCPChannel(evbase, fd, 0, 0, nullptr, ChannelState::SOCKET_CONNECTED, false)
{
    CHECK_GE(fd_, 0);
    _initialize_read_write_events();
}

int
TCPChannel::start_connecting(StreamChannelConnectObserver* observer,
                             struct timeval *connect_timeout)
{
    CHECK_EQ(state_, ChannelState::INIT);
    CHECK(is_client_);

    VLOG(2) << "start connecting...";

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

    // socket_ev_.reset(
    //     event_new(evbase_, fd_, EV_READ | EV_WRITE | EV_PERSIST,
    //               s_socket_eventcb, this));
    // CHECK(socket_ev_);

    // rv = event_add(socket_ev_.get(), connect_timeout);
    // CHECK_EQ(rv, 0);

    state_ = ChannelState::CONNECTING_SOCKET;

    _initialize_read_write_events();

    return 0;
}

void
TCPChannel::set_observer(StreamChannelObserver* observer)
{
    CHECK_EQ(state_, ChannelState::ESTABLISHED);
    CHECK(!observer_);
    observer_ = observer;
}

int
TCPChannel::read(uint8_t *data, size_t len)
{
    // CHECK(in_buffered_mode_);
    return evbuffer_remove(input_evb_.get(), data, len);
}

int
TCPChannel::read_buffer(struct evbuffer* buf, size_t len)
{
    // CHECK(in_buffered_mode_);
    return evbuffer_remove_buffer(input_evb_.get(), buf, len);
}

int
TCPChannel::drain(size_t len)
{
    // CHECK(in_buffered_mode_);
    return evbuffer_drain(input_evb_.get(), len);
}

uint8_t*
TCPChannel::peek(ssize_t len)
{
    // CHECK(in_buffered_mode_);
    return evbuffer_pullup(input_evb_.get(), len);
}

size_t
TCPChannel::get_avail_input_length() const
{
    // CHECK(in_buffered_mode_);
    return evbuffer_get_length(input_evb_.get());
}

size_t
TCPChannel::get_output_length() const
{
    // CHECK(in_buffered_mode_);
    return evbuffer_get_length(output_evb_.get());
}

void
TCPChannel::set_read_watermark(size_t lowmark, size_t highmark)
{
    CHECK(0);
}

int
TCPChannel::write(const uint8_t *data, size_t size)
{
    // CHECK(in_buffered_mode_);
    return evbuffer_add(output_evb_.get(), data, size);
}

int
TCPChannel::write_buffer(struct evbuffer* buf)
{
    // CHECK(in_buffered_mode_);
    return evbuffer_add_buffer(output_evb_.get(), buf);
}

void
TCPChannel::close()
{
    state_ = ChannelState::CLOSED;
    socket_ev_.reset();
    input_evb_.reset(); // XXX/maybe we can keep the input buf for
                        // client to read
    output_evb_.reset();
    if (fd_) {
        close(fd_);
    }
    tamaraw_timer_ev_.reset();
}

bool
TCPChannel::is_closed() const
{
    return state_ == ChannelState::CLOSED;
}

/********************/

void
TCPChannel::_initialize_read_write_events()
{
    CHECK_GE(fd_, 0);
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
TCPChannel::_on_socket_readcb(int fd, short what)
{
    CHECK_EQ(fd, fd_);

    VLOG(2) << "begin";

    DestructorGuard dg(this);

    if (what & EV_READ) {
        // if (in_buffered_mode_) {
            const auto rv = evbuffer_read(input_evb_.get(), fd_, -1);
            VLOG(2) << "evbuffer_read() returns: " << rv;
            if (rv == 0) {
                observer_->onEOF(this);
            } else if ((rv == -1) && (errno != EAGAIN)) {
                observer_->onError(this, errno);
            // }
        } else {
            observer_->onReadable(this);
        }
    } else {
        DCHECK(0) << "invalid events: " << what;
    }

    VLOG(2) << "done";
}

void
TCPChannel::_on_socket_writecb(int fd, short what)
{
    CHECK_EQ(fd, fd_);

    VLOG(2) << "begin";

    DestructorGuard dg(this);

    if (what & EV_WRITE) {
        // if (in_buffered_mode_) {
            const auto rv = evbuffer_write(output_evb_.get(), fd_);
            if (rv == 0) {
                observer_->onEOF(this);
            } else if ((rv == -1) && (errno != EAGAIN)) {
                observer_->onError(this, errno);
            }
        } else {
            observer_->onWritable(this);
        }
    } else {
        DCHECK(0) << "invalid events: " << what;
    }

    VLOG(2) << "done";
}

// int
// TCPChannel::set_non_buffer_mode(bool enable)
// {
//     if (in_buffered_mode_ && enable) {
//         // currently in buffered mode and wanna switch to non-buffered
//         // mode, so make sure the buffers are empty
//         CHECK_EQ(evbuffer_get_length(input_evb_.get(), 0));
//         CHECK_EQ(evbuffer_get_length(output_evb_.get(), 0));
//     }
//     in_buffered_mode_ = !enable;
// }

// int
// TCPChannel::nb_read(uint8_t *data, size_t len)
// {
//     CHECK(!in_buffered_mode_);
//     CHECK_LT(len, SSIZE_MAX);
//     return ::read(fd_, data, len);
// }

// int
// TCPChannel::nb_read_buffer(struct evbuffer* buf, int len)
// {
//     CHECK(!in_buffered_mode_);
//     return evbuffer_read(buf, fd_, len);
// }

// ssize_t
// TCPChannel::nb_write(const uint8_t *data, size_t size)
// {
//     CHECK(!in_buffered_mode_);
//     CHECK_GT(size, 0);
//     return ::write(fd_, data, size);
// }

// int
// TCPChannel::nb_write_buffer(struct evbuffer* buf)
// {
//     CHECK(!in_buffered_mode_);
//     return evbuffer_write(fd_, buf);
// }

TCPChannel::TCPChannel(struct event_base *evbase, int fd,
                       const in_addr_t& addr, const in_port_t& port,
                       StreamChannelObserver* observer,
                       ChannelState starting_state, bool is_client)
    : evbase_(evbase), observer_(nullptr), connect_observer_(nullptr),
    , state_(starting_state), fd_(fd)
    , socket_read_ev_(nullptr, event_free)
    , socket_write_ev_(nullptr, event_free)
    // , in_buffered_mode_(true)
    , addr_(addr), port_(port), is_client_(is_client)
    , input_evb_(nullptr, evbuffer_free)
    , output_evb_(nullptr, evbuffer_free)
    , tamaraw_timer_ev_(nullptr, event_free)
{}

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
