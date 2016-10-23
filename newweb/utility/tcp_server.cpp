
#include "easylogging++.h"
#include "tcp_channel.hpp"
#include "tcp_server.hpp"

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) << "tcpCh= " << (inst)->objId() << " "
#define vlogself(level) vloginst(level, this)

#define loginst(level, inst) LOG(level) << "tcpCh= " << (inst)->objId() << " "
#define logself(level) loginst(level, this)

namespace myio
{

TCPServer::TCPServer(
    struct event_base* evbase,
    const in_addr_t& addr, const in_port_t& port,
    StreamServerObserver* observer,
    const bool start_listening
    )
    : evbase_(evbase), observer_(observer), addr_(addr), port_(port)
    , state_(ServerState::INIT)
    , evlistener_(nullptr, evconnlistener_free)
    , listening_(false)

#ifndef IN_SHADOW
    , ssl_ctx_(nullptr)
#endif
{

    /* create socket and manually bind so that we don't specify
     * SO_KEEPALIVE. evconnlistener_new_bind() uses SO_KEEPALIVE,
     * which shadow doesn't support
     */

	fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	CHECK_GT(fd_, 0);

    int rv = 0;
    rv = evutil_make_listen_socket_reuseable(fd_);
    CHECK_EQ(rv, 0);

    struct sockaddr_in server;
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = addr_;
    server.sin_port = htons(port_);

	rv = bind(fd_, (struct sockaddr *) &server, sizeof(server));
	CHECK_EQ(rv, 0);

    if (start_listening) {
        _start_listening();
    }
}

bool
TCPServer::start_accepting()
{
    CHECK(listening_);

    CHECK((state_ == ServerState::INIT) || (state_ == ServerState::PAUSED));

    auto rv = evconnlistener_enable(evlistener_.get());
    CHECK_EQ(rv, 0);

    state_ = ServerState::ACCEPTING;

    vlogself(2) << "tcpserver have started accepting";
    return !!evlistener_;
}

#ifndef IN_SHADOW
bool
TCPServer::start_accepting_ssl(SSL_CTX* ssl_ctx)
{
    CHECK(listening_);
    CHECK(!ssl_ctx_ || (ssl_ctx_ == ssl_ctx));

    ssl_ctx_ = ssl_ctx;

    CHECK((state_ == ServerState::INIT) || (state_ == ServerState::PAUSED));

    auto rv = evconnlistener_enable(evlistener_.get());
    CHECK_EQ(rv, 0);

    state_ = ServerState::ACCEPTING;

    vlogself(2) << "tcpserver have started accepting";
    return !!evlistener_;
}
#endif

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
    vlogself(2) << "got a client conn, fd= " << fd;

    DestructorGuard dg(this);

    CHECK_EQ(state_, ServerState::ACCEPTING);
    CHECK_EQ(evlistener_.get(), listener);

    auto rv = evutil_make_socket_nonblocking(fd);
    CHECK_EQ(rv, 0);

#ifdef IN_SHADOW

    TCPChannel::UniquePtr channel(new TCPChannel(evbase_, fd));
    observer_->onAccepted(this, std::move(channel));

#else
    if (!ssl_ctx_) {
        TCPChannel::UniquePtr channel(new TCPChannel(evbase_, fd));
        observer_->onAccepted(this, std::move(channel));
    } else {
        std::shared_ptr<TCPChannel> channel(new TCPChannel(evbase_, fd),
                                            folly::DelayedDestruction::Destructor());

        const auto ret = in_ssl_handshakes_.insert(
            std::make_pair(channel->objId(), channel));
        CHECK(ret.second);

        channel->start_ssl(ssl_ctx_);
    }
#endif

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

bool
TCPServer::start_listening()
{
    if (listening_) {
        logself(WARNING) << "is already listening";
    } else {
        _start_listening();
    }
    return true;
}

void
TCPServer::_start_listening()
{
    CHECK(!listening_);

    static const auto backlog = 20;

    auto rv = listen(fd_, backlog);
    CHECK_EQ(rv, 0) << "listen(fd=" << fd_ << ") fails :( rv= " << rv
                    << " errno= " << errno << " (" << strerror(errno) << ")";

    CHECK(!evlistener_);
    evlistener_.reset(
        evconnlistener_new(
            evbase_, s_listener_acceptcb, this,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
            backlog, fd_));
    CHECK_NOTNULL(evlistener_);

    rv = evconnlistener_disable(evlistener_.get());
    CHECK_EQ(rv, 0);

    listening_ = true;
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
