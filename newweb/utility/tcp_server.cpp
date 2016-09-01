
#include "easylogging++.h"
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
    CHECK_EQ(state_, ServerState::INIT);

    /* create socket and manually bind so that we don't specify
     * SO_KEEPALIVE. evconnlistener_new_bind() uses SO_KEEPALIVE,
     * which shadow doesn't support
     */

	const auto fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	CHECK_GT(fd, 0);

    int rv = 0;
    rv = evutil_make_listen_socket_reuseable(fd);
    CHECK_EQ(rv, 0);

    struct sockaddr_in server;
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = addr_;
    server.sin_port = htons(port_);

	rv = bind(fd, (struct sockaddr *) &server, sizeof(server));
	CHECK_EQ(rv, 0);

    static const auto backlog = 1000;

	rv = listen(fd, backlog); // hardcode for now
    CHECK_EQ(rv, 0);

    CHECK(!evlistener_);
    evlistener_.reset(
        evconnlistener_new(
            evbase_, s_listener_acceptcb, this,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
            backlog, fd));
    CHECK_NOTNULL(evlistener_);

    state_ = ServerState::ACCEPTING;

    VLOG(2) << "tcpserver have started accepting";
    return !!evlistener_;
}

bool
TCPServer::pause_accepting()
{
    CHECK_EQ(state_, ServerState::ACCEPTING);
    const auto rv = evconnlistener_disable(evlistener_.get());
    CHECK_EQ(rv, 0);
    state_ = ServerState::PAUSED;
    return true;
}

void
TCPServer::set_observer(myio::StreamServerObserver* observer)
{
    observer_ = observer;
}

void
TCPServer::on_conn_accepted(
    struct evconnlistener *listener,
    int fd, struct sockaddr *addr, int len)
{
    DestructorGuard dg(this);

    CHECK_EQ(state_, ServerState::ACCEPTING);
    CHECK_EQ(evlistener_.get(), listener);

    auto rv = evutil_make_socket_nonblocking(fd);
    CHECK_EQ(rv, 0);

    TCPChannel::UniquePtr channel(new TCPChannel(evbase_, fd));
    observer_->onAccepted(this, std::move(channel));
}

void
TCPServer::on_accept_error(
    struct evconnlistener *listener, int errorcode)
{
    DestructorGuard dg(this);

    CHECK_EQ(state_, ServerState::ACCEPTING);
    CHECK_EQ(evlistener_.get(), listener);

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
