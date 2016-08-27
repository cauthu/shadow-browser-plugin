
#include "myassert.h"
#include "tcp_channel.hpp"
#include "tcp_server.hpp"

namespace myio
{

TCPServer::TCPServer(
    struct event_base* evbase,
    const in_addr_t& addr, const in_port_t& port,
    StreamServerObserver* observer
    )
    : evbase_(evbase), observer_(observer), addr_(addr), port_(port)
    , state_(ServerState::INIT)
    , evlistener_(nullptr, evconnlistener_free)
{
}

TCPServer::~TCPServer()
{}

bool
TCPServer::start_accepting()
{
    myassert(state_ == ServerState::INIT);

    struct sockaddr_in server;
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = addr_;
    server.sin_port = htons(port_);

    myassert(!evlistener_);
    evlistener_.reset(
        evconnlistener_new_bind(
            evbase_, s_listener_acceptcb, this,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
            -1, (struct sockaddr*)&server, sizeof(server)));

    if (evlistener_) {
        state_ = ServerState::ACCEPTING;
    } else {
        state_ = ServerState::CLOSED;
    }

    return !!evlistener_;
}

void
TCPServer::on_conn_accepted(
    struct evconnlistener *listener,
    int fd, struct sockaddr *addr, int len)
{
    DestructorGuard dg(this);

    myassert(state_ == ServerState::ACCEPTING);
    myassert(evlistener_.get() == listener);

    TCPChannel::UniquePtr channel(new TCPChannel(evbase_, fd));
    observer_->onAccepted(this, std::move(channel));
}

void
TCPServer::on_accept_error(
    struct evconnlistener *listener, int errorcode)
{
    DestructorGuard dg(this);

    myassert(state_ == ServerState::ACCEPTING);
    myassert(evlistener_.get() == listener);

    observer_->onAcceptError(this, errorcode);
}

void
TCPServer::s_listener_acceptcb(
    struct evconnlistener *listener,
    int fd, struct sockaddr *addr, int len, void *arg)
{
    auto serverlistener = (TCPServer*)arg;
    serverlistener->on_conn_accepted(listener, fd, addr, len);
}

void
TCPServer::s_listener_errorcb(
    struct evconnlistener *listener, void *arg)
{
    auto serverlistener = (TCPServer*)arg;
    serverlistener->on_accept_error(listener, EVUTIL_SOCKET_ERROR());
}

}
