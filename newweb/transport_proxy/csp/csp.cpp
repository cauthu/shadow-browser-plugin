

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
using myio::buflo::BufloMuxChannelImplSpdy;


CSPHandler::CSPHandler(struct event_base* evbase,
                               StreamServer::UniquePtr streamserver,
                               const in_addr_t& peer_addr,
                               const in_port_t& peer_port,
                               const in_addr_t& socks5_addr,
                               const in_port_t& socks5_port)
    : evbase_(evbase)
    , stream_server_(std::move(streamserver))
    , peer_addr_(peer_addr), peer_port_(peer_port)
    , socks5_addr_(socks5_addr), socks5_port_(socks5_port)
    , peer_fd_(-1)
{
    stream_server_->set_observer(this);

    // connect to peer first
    if (socks5_addr_) {
        peer_channel_.reset(new TCPChannel(evbase_, socks5_addr_, socks5_port_, nullptr));
        state_ = State::PROXY_CONNECTING;
    }
    const auto rv = peer_channel_->start_connecting(this);
    CHECK_EQ(rv, 0);
}

void
CSPHandler::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    // a new client that we should proxy
}

void
CSPHandler::onAcceptError(StreamServer*, int errorcode) noexcept
{
    LOG(WARNING) << "CSPHandler has accept error: " << strerror(errorcode);
}

void
CSPHandler::onConnected(StreamChannel* ch) noexcept
{
    if (state_ == State::PROXY_CONNECTING) {
        vlogself(2) << "now connected to the PROXY";

        state_ = State::PROXY_CONNECTED;

        vlogself(2) << "now tell proxy to connect to peer";
        CHECK(!socks_connector_);
        socks_connector_.reset(
            new Socks5Connector(std::move(peer_channel_),
                                peer_addr_, peer_port_));
        auto rv = socks_connector_->start_connecting(this);
        CHECK(!rv);
        state_ = State::CONNECTING;
    }
    else if (state_ == State::CONNECTING) {
        vlogself(2) << "connected to peer";
        state_ = State::CONNECTED;
        _on_connected_to_server_peer();
    }
    else {
        logself(FATAL) << "invalid state";
    }
}

void
CSPHandler::onSocksTargetConnectResult(
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
        state_ = State::CONNECTED;

        _on_connected_to_server_peer();

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
CSPHandler::_on_connected_to_server_peer()
{
    peer_fd_ = peer_channel_->release_fd();
    CHECK_GT(peer_fd_, 0);

    peer_channel_.reset();

    buflo_ch_.reset(
        new BufloMuxChannelImplSpdy(
            evbase_, peer_fd_, true, 512,
            boost::bind(&CSPHandler::_on_buflo_channel_pair_result_cb,
                        this, _1, _2),
            boost::bind(&CSPHandler::_on_buflo_channel_closed_cb,
                        this, _1),
            boost::bind(&CSPHandler::_on_buflo_channel_stream_create_result_cb,
                        this, _1, _2, _3),
            boost::bind(&CSPHandler::_on_buflo_channel_stream_data_cb,
                        this, _1, _2),
            boost::bind(&CSPHandler::_on_buflo_channel_stream_closed_cb,
                        this, _1, _2)
            ));
    CHECK_NOTNULL(buflo_ch_.get());

    // now we can start accepting client connections
    stream_server_->set_observer(this);
    auto rv = stream_server_->start_accepting();
    CHECK(rv);
}

void
CSPHandler::onConnectError(StreamChannel* ch, int errorcode) noexcept
{
    LOG(FATAL) << "to be implemented";
}

void
CSPHandler::onConnectTimeout(StreamChannel*) noexcept
{
    LOG(FATAL) << "to be implemented";
}

void
CSPHandler::_on_buflo_channel_pair_result_cb(myio::buflo::BufloMuxChannel*,
                                             bool ok)
{
}

void
CSPHandler_on_buflo_channel_closed_cb(myio::buflo::BufloMuxChannel*) {}
    void _on_buflo_channel_stream_create_result_cb(myio::buflo::BufloMuxChannel*,
                                                   bool, int) {}
    void _on_buflo_channel_stream_data_cb(myio::buflo::BufloMuxChannel*,
                                          int) {}
    void _on_buflo_channel_stream_closed_cb(myio::buflo::BufloMuxChannel*,
                                            int) {}

CSPHandler::~CSPHandler()
{
    LOG(FATAL) << "not reached";
}

