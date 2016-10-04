

#include <boost/bind.hpp>

#include "../../utility/tcp_server.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/buflo_mux_channel_impl_spdy.hpp"

#include "csp.hpp"


#define _LOG_PREFIX(inst) << "cstp= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


using myio::TCPChannel;
using myio::StreamServer;
using myio::StreamChannel;
using myio::Socks5Connector;
using myio::buflo::BufloMuxChannel;
using myio::buflo::BufloMuxChannelImplSpdy;


namespace csp
{

ClientSideProxy::ClientSideProxy(struct event_base* evbase,
                               StreamServer::UniquePtr streamserver,
                               const char* peer_host,
                               const in_port_t& peer_port,
                               const in_addr_t& socks5_addr,
                               const in_port_t& socks5_port)
    : evbase_(evbase)
    , stream_server_(std::move(streamserver))
    , peer_host_(peer_host), peer_port_(peer_port)
    , socks5_addr_(socks5_addr), socks5_port_(socks5_port)
    , state_(State::INITIAL)
{
    LOG(INFO) << "NOT accepting client connections until we're connected to the SSP";
}

ClientSideProxy::EstablishReturnValue
ClientSideProxy::establish_tunnel(CSPReadyCb ready_cb,
                                  const bool forceReconnect)
{
    vlogself(2) << "begin";

    CHECK((state_ == State::INITIAL) || (state_ == State::READY));

    ready_cb_ = ready_cb;

    if (state_ == State::READY) {
        vlogself(2) << "currently ready";
        if (!forceReconnect) {
            return EstablishReturnValue::ALREADY_READY;
        }

        vlogself(2) << "being forced to reestablish";

        CHECK(!socks_connector_);
        CHECK(!peer_channel_);

        auto rv = stream_server_->pause_accepting();
        CHECK(rv);

        /* note that this calls destructors of the client handler, and
         * those destructors used to notify our
         * _on_client_handler_done() which will retry to delete the
         * handler again causing double free. the fix we have employed
         * is to make the handler not notify us when they're being
         * destroyed. an alternative fix is for us to swap the
         * client_handlers_ map with a local here that we clear, so
         * when notified later, our client_handlers_ is empty and so
         * we won't double free the handler */
        client_handlers_.clear();

        buflo_ch_.reset();
    }

    state_ = State::INITIAL;

    // connect to peer first
    if (socks5_addr_) {
        peer_channel_.reset(
            new TCPChannel(evbase_, socks5_addr_, socks5_port_, nullptr));
        state_ = State::PROXY_CONNECTING;
    } else {
        const auto peer_addr = common::getaddr(peer_host_.c_str());
        peer_channel_.reset(
            new TCPChannel(evbase_, peer_addr, peer_port_, nullptr));
        state_ = State::CONNECTING;
    }

    struct timeval timeout_tv = {5, 0};
    const auto rv = peer_channel_->start_connecting(this, &timeout_tv);
    CHECK_EQ(rv, 0);

    vlogself(2) << "returning pending";
    return EstablishReturnValue::PENDING;
}

void
ClientSideProxy::set_auto_start_defense_session_on_next_send()
{
    CHECK_EQ(state_, State::READY);
    CHECK_NOTNULL(buflo_ch_.get());

    buflo_ch_->set_auto_start_defense_session_on_next_send();
}

void
ClientSideProxy::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    vlogself(2) << "a new proxy client";

    CHECK_EQ(state_, State::READY) << "for simplicity, should start client traffic only once the proxy is ready";

    ClientHandler::UniquePtr chandler(
        new ClientHandler(std::move(channel), buflo_ch_.get(),
                          boost::bind(&ClientSideProxy::_on_client_handler_done,
                                      this, _1)));
    const auto chid = chandler->objId();
    const auto ret = client_handlers_.insert(
        make_pair(chid, std::move(chandler)));
    CHECK(ret.second); // insist it was newly inserted
}

void
ClientSideProxy::onAcceptError(StreamServer*, int errorcode) noexcept
{
    LOG(WARNING) << "ClientSideProxy has accept error: " << strerror(errorcode);
}

void
ClientSideProxy::onConnected(StreamChannel* ch) noexcept
{
    if (state_ == State::PROXY_CONNECTING) {
        vlogself(2) << "now connected to the PROXY";

        state_ = State::PROXY_CONNECTED;

        vlogself(2) << "now tell proxy to connect to peer";
        CHECK(!socks_connector_);

        /* due to issue
         * https://bitbucket.org/hatswitch/shadow-plugin-extras/issues/3/
         * we do local lookup of the ssp's ip address here, even we'd
         * prefer to give Socks5Connector() the hostname, which will
         * let tor exit do resolution
         */
        const auto peer_addr = common::getaddr(peer_host_.c_str());
        CHECK_NE(peer_addr, 0);

        socks_connector_.reset(
            new Socks5Connector(std::move(peer_channel_),
                                peer_addr, peer_port_));
        auto rv = socks_connector_->start_connecting(this);
        CHECK(!rv);
        state_ = State::CONNECTING;
    }
    else if (state_ == State::CONNECTING) {
        vlogself(2) << "connected to peer";
        state_ = State::READY;
        _on_connected_to_ssp();
    }
    else {
        logself(FATAL) << "invalid state";
    }
}

void
ClientSideProxy::onSocksTargetConnectResult(
    Socks5Connector* connector,
    Socks5ConnectorObserver::ConnectResult result) noexcept
{
    switch (result) {
    case Socks5ConnectorObserver::ConnectResult::OK: {
        CHECK_EQ(state_, State::CONNECTING);
        CHECK_EQ(socks_connector_.get(), connector);

        // get back our transport
        CHECK(!peer_channel_);
        StreamChannel::UniquePtr tmp = socks_connector_->release_transport();
        peer_channel_.reset((TCPChannel*)tmp.release());
        CHECK_NOTNULL(peer_channel_);
        socks_connector_.reset();

        vlogself(2) << "connected to target (thru socks proxy)";
        state_ = State::READY;

        _on_connected_to_ssp();

        break;
    }

    case Socks5ConnectorObserver::ConnectResult::ERR_FAIL:
        logself(FATAL) << "to implement";
        break;

    default:
        logself(FATAL) << "invalid result";
        break;
    }
}

void
ClientSideProxy::_on_connected_to_ssp()
{
    const auto peer_fd = peer_channel_->release_fd();
    CHECK_GT(peer_fd, 0);

    peer_channel_.reset();

    buflo_ch_.reset(
        new BufloMuxChannelImplSpdy(
            evbase_, peer_fd, true, 512,
            boost::bind(&ClientSideProxy::_on_buflo_channel_status,
                        this, _1, _2),
            NULL
            ));
    CHECK_NOTNULL(buflo_ch_.get());
}

void
ClientSideProxy::onConnectError(StreamChannel* ch, int errorcode) noexcept
{
    LOG(FATAL) << "error connecting to SSP: ["
               << evutil_socket_error_to_string(errorcode) << "]";
}

void
ClientSideProxy::onConnectTimeout(StreamChannel*) noexcept
{
    LOG(FATAL) << "timed out connecting to SSP";
}

void
ClientSideProxy::_on_buflo_channel_status(BufloMuxChannel*,
                                          BufloMuxChannel::ChannelStatus status)
{
    if (status == BufloMuxChannel::ChannelStatus::READY) {
        DestructorGuard dg(this);

        // now we can start accepting client connections
        stream_server_->set_observer(this);
        auto rv = stream_server_->start_accepting();
        CHECK(rv);

        ready_cb_(this);
    } else if (status == BufloMuxChannel::ChannelStatus::CLOSED) {
        LOG(FATAL) << "buflo channel is closed, so we're exiting";
    } else {
        LOG(FATAL) << "unknown channel status";
    }
}

void
ClientSideProxy::_on_client_handler_done(ClientHandler* chandler)
{
    vlogself(2) << "chandler done; remove it";
    client_handlers_.erase(chandler->objId());
}

ClientSideProxy::~ClientSideProxy()
{
    LOG(FATAL) << "to do";
}

} // namespace csp
