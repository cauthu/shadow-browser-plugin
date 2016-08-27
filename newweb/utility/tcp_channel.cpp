

#include <event2/event.h>

#include "tcp_channel.hpp"
#include "myassert.h"

namespace myio {

// TCPClientChannel::TCPClientChannel(
//     struct event_base *evbase,
//     const in_addr_t& addr, const in_port_t& port,
//     TCPChannelObserver* observer
//     )
//     : TCPChannel(evbase, observer)
//     , addr_(addr), port_(port)
//     , connect_state_(ConnectState::INIT)
//     , connect_observer_(nullptr)
// {
// }


// TCPClientChannel::~TCPClientChannel()
// {
// }


// bool
// TCPClientChannel::start_connecting(TCPChannelConnectObserver* observer)
// {
//     myassert(base_state_ == ChannelState::INIT);
//     myassert(connect_state_ == ConnectState::INIT);

//     connect_observer_ = observer;
//     myassert(connect_observer_);

//     struct sockaddr_in server;
//     bzero(&server, sizeof(server));
//     server.sin_family = AF_INET;
//     server.sin_addr.s_addr = addr_;
//     server.sin_port = htons(port_);

//     _setup_bufev(-1);

//     auto rv = bufferevent_enable(bufev_.get(), EV_READ|EV_WRITE);
//     myassert(!rv);

//     rv = bufferevent_socket_connect(
//         bufev_.get(), (struct sockaddr *)&server, sizeof(server));

//     if (!rv) {
//         connect_state_ = ConnectState::CONNECTING;
//     }

//     return (0 == rv);
// }

// void
// TCPClientChannel::on_bufev_event_connected(struct bufferevent *bev)
// {
//     myassert(connect_state_ == ConnectState::CONNECTING);
//     connect_state_ = ConnectState::CONNECTED;
//     connect_observer_->onConnected(this);
//     connect_observer_ = nullptr;
// }

// void
// TCPClientChannel::on_bufev_event_error(struct bufferevent *bev, int errorcode)
// {
//     DestructorGuard dg(this); // safe even if there's currently one
//                               // already on stack

//     if (connect_state_ == ConnectState::CONNECTING) {
//         connect_state_ = ConnectState::ERROR;
//         connect_observer_->onConnectError(this, errorcode);
//         connect_observer_ = nullptr;
//     } else {
//         TCPChannel::on_bufev_event_error(bev, errorcode);
//     }
// }

/*******************************************/

TCPChannel::TCPChannel(
    struct event_base *evbase, StreamChannelObserver* observer)
    : evbase_(evbase), observer_(observer)
    , bufev_(nullptr, bufferevent_free)
    , state_(ChannelState::INIT)
{
    myassert(observer);
}

TCPChannel::TCPChannel(
    struct event_base *evbase, int fd)
    : evbase_(evbase), observer_(nullptr)
    , bufev_(nullptr, bufferevent_free)
    , state_(ChannelState::ESTABLISHED)
{
    _setup_bufev(fd);
}

void
TCPChannel::set_channel_observer(StreamChannelObserver* observer)
{
    myassert(state_ == ChannelState::ESTABLISHED);
    myassert(!observer_);
    observer_ = observer;
}

int
TCPChannel::start_connecting(const in_addr_t& addr, const in_port_t& port,
                             TCPChannelConnectObserver* observer)
{
    myassert(state_ == ChannelState::INIT);
    // myassert(connect_state_ == ConnectState::INIT);

    connect_observer_ = observer;
    myassert(connect_observer_);

    struct sockaddr_in server;
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = addr;
    server.sin_port = htons(port);

    _setup_bufev(-1);

    auto rv = bufferevent_enable(bufev_.get(), EV_READ|EV_WRITE);
    myassert(!rv);

    rv = bufferevent_socket_connect(
        bufev_.get(), (struct sockaddr *)&server, sizeof(server));

    if (!rv) {
        // connect_state_ = ConnectState::CONNECTING;
        state_ = ChannelState::CONNECTING;
    } else {
        state_ = ChannelState::CLOSED;
    }

    return rv;
}

size_t
TCPChannel::read(uint8_t *data, size_t size)
{
    myassert(state_ == ChannelState::ESTABLISHED);
    myassert(bufev_);
    return bufferevent_read(bufev_.get(), data, size);
}

int
TCPChannel::read_buffer(struct evbuffer* buf)
{
    myassert(state_ == ChannelState::ESTABLISHED);
    myassert(bufev_);
    return bufferevent_read_buffer(bufev_.get(), buf);
}

int
TCPChannel::drain(size_t len)
{
    myassert(state_ == ChannelState::ESTABLISHED);
    myassert(bufev_);
    return evbuffer_drain(bufferevent_get_input(bufev_.get()), len);
}

uint8_t*
TCPChannel::peek(size_t len)
{
    myassert(state_ == ChannelState::ESTABLISHED);
    myassert(bufev_);
    return evbuffer_pullup(bufferevent_get_input(bufev_.get()), len);
}

size_t
TCPChannel::get_avail_input_length() const
{
    myassert(bufev_);
    return evbuffer_get_length(bufferevent_get_input(bufev_.get()));
}

void
TCPChannel::set_read_watermark(size_t lowmark, size_t highmark)
{
    myassert(bufev_);
    bufferevent_setwatermark(bufev_.get(), EV_READ, lowmark, highmark);
}

int
TCPChannel::write_buffer(struct evbuffer* buf)
{
    // myassert(state_ == ChannelState::ESTABLISHED);
    myassert(bufev_);
    return bufferevent_write_buffer(bufev_.get(), buf);
}

int
TCPChannel::write(const uint8_t *data, size_t size)
{
    // myassert((state_ == ChannelState::ESTABLISHED) || (state_ == );
    myassert(bufev_);
    return bufferevent_write(bufev_.get(), data, size);
}

void
TCPChannel::close()
{
    // bufev_ had better have a deleter
    bufev_.reset();
}

void
TCPChannel::_setup_bufev(int fd)
{
    myassert(!bufev_);
    bufev_.reset(bufferevent_socket_new(
                     evbase_, fd, (BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS)));
    myassert(bufev_);

    bufferevent_setcb(
        bufev_.get(), s_bufev_readcb, s_bufev_writecb, s_bufev_eventcb, this);
}

void
TCPChannel::on_bufev_event(struct bufferevent *bev, short what)
{
    DestructorGuard dg(this);
    myassert(bufev_.get() == bev);

    auto handled = false; // whether we've matched some event type and
                          // handled it

    // we expect BEV_EVENT_CONNECTED, BEV_EVENT_ERROR, and
    // BEV_EVENT_EOF are mutually exclusive
    if (what & BEV_EVENT_CONNECTED) {
        myassert(!handled);
        on_bufev_event_connected(bev);
        handled = true;
    }
    if (what & BEV_EVENT_ERROR) {
        myassert(!handled);
        on_bufev_event_error(bev, EVUTIL_SOCKET_ERROR());
        handled = true;
    }
    if (what & BEV_EVENT_EOF) {
        myassert(!handled);
        state_ = ChannelState::CLOSED;
        on_bufev_event_eof(bev);
        handled = true;
    }
}

void
TCPChannel::on_bufev_event_connected(struct bufferevent *bev)
{
    myassert(state_ == ChannelState::CONNECTING);
    state_ = ChannelState::ESTABLISHED;
    myassert(connect_observer_);
    connect_observer_->onConnected(this);
    connect_observer_ = nullptr;
}

void
TCPChannel::on_bufev_event_eof(struct bufferevent *bev)
{
    myassert(!connect_observer_);
    state_ = ChannelState::CLOSED;
    observer_->onEOF(this);
}

void
TCPChannel::on_bufev_event_error(struct bufferevent *bev, int errorcode)
{
    auto prevstate = state_;
    state_ = ChannelState::CLOSED;

    if (prevstate == ChannelState::CONNECTING) {
        connect_observer_->onConnectError(this, errorcode);
        connect_observer_ = nullptr;
    } else {
        observer_->onError(this, errorcode);
    }
}

void
TCPChannel::on_bufev_read(struct bufferevent *bev)
{
    DestructorGuard dg(this);
    myassert(bufev_.get() == bev);
    observer_->onNewReadDataAvailable(this);
}

void
TCPChannel::on_bufev_write(struct bufferevent *bev)
{
    DestructorGuard dg(this);
    myassert(bufev_.get() == bev);
    observer_->onWrittenData(this);
}

void
TCPChannel::s_bufev_eventcb(struct bufferevent *bev, short events, void *ptr)
{
    TCPChannel* channel = (TCPChannel*)ptr;
    channel->on_bufev_event(bev, events);
}

void
TCPChannel::s_bufev_readcb(struct bufferevent *bev, void *ptr)
{
    TCPChannel* channel = (TCPChannel*)ptr;
    channel->on_bufev_read(bev);
}

void
TCPChannel::s_bufev_writecb(struct bufferevent *bev, void *ptr)
{
    TCPChannel* channel = (TCPChannel*)ptr;
    channel->on_bufev_write(bev);
}

/**************************************************/


} // end myio namespace
