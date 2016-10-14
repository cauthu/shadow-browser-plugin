

#include <utility>
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
                                 const in_port_t& socks5_port,
                                 const uint32_t& buflo_frequencyMs,
                                 const uint32_t& buflo_L,
                                 const uint32_t& buflo_time_limit_secs)
    : evbase_(evbase)
    , stream_server_(std::move(streamserver))
    , peer_host_(peer_host), peer_port_(peer_port)
    , socks5_addr_(socks5_addr), socks5_port_(socks5_port)
    , buflo_frequencyMs_(buflo_frequencyMs)
    , buflo_L_(buflo_L)
    , buflo_time_limit_secs_(buflo_time_limit_secs)
    , state_(State::INITIAL)
    , all_recv_byte_count_so_far_(0)
    , useful_recv_byte_count_so_far_(0)
    , num_whole_dummy_cells_dropped_so_far_(0)
    , myaddr_(INADDR_NONE)
    , log_stats_timer_(
        new Timer(evbase_, true,
                  boost::bind(&ClientSideProxy::_log_stats_timer_fired,
                              this, _1)))
{
    static bool initialized = false;

    CHECK(!initialized) << "there should be ONLY one csp per process";

    LOG(INFO) << "NOT accepting client connections until we're connected to the SSP";
    CHECK(!stream_server_->is_listening());

    // get my ip address, in host byte order
    char myhostname[80] = {0};
    const auto rv = gethostname(myhostname, (sizeof myhostname) - 1);
    CHECK_EQ(rv, 0);

    myaddr_ = ntohl(common::getaddr(myhostname));
    CHECK(myaddr_ && (myaddr_ != INADDR_NONE));

    _schedule_log_timer();

    initialized = true;
}

ClientSideProxy::EstablishReturnValue
ClientSideProxy::establish_tunnel(CSPStatusCb status_cb,
                                  const bool forceReconnect)
{
    vlogself(2) << "begin";

    // we can reconnect even if there is already one in
    // progress... just reset() all the unique ptrs and things SHOULD
    // work correctly

    csp_status_cb_ = status_cb;

    // currently we expect the driver to always force reconnect
    CHECK(forceReconnect) << "todo";

    _update_stats();

    _reset_to_initial();

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

    logself(INFO) << "begin (re)establishing channel to ssp...";

    struct timeval timeout_tv = {5, 0};
    const auto rv = peer_channel_->start_connecting(this, &timeout_tv);
    CHECK_EQ(rv, 0);

    vlogself(2) << "returning pending";
    return EstablishReturnValue::PENDING;
}

bool
ClientSideProxy::set_auto_start_defense_session_on_next_send()
{
    if (state_ != State::READY) {
        logself(WARNING)
            << "cannot set auto start because channel is not ready";
        return false;
    }

    CHECK_NOTNULL(buflo_ch_.get());

    buflo_ch_->set_auto_start_defense_session_on_next_send();

    logself(INFO) << "will start defense on next send";
    return true;
}

void
ClientSideProxy::stop_defense_session(const bool& right_now)
{
    if (state_ != State::READY) {
        logself(WARNING) << "channel is not ready";
        return;
    }
    CHECK_NOTNULL(buflo_ch_.get());

    buflo_ch_->stop_defense_session(right_now);
}

const uint64_t
ClientSideProxy::all_recv_byte_count_so_far() const
{
    const auto from_channel = buflo_ch_ ? buflo_ch_->all_recv_byte_count() : 0;
    return all_recv_byte_count_so_far_ + from_channel;
}

const uint64_t
ClientSideProxy::useful_recv_byte_count_so_far() const
{
    const auto from_channel = buflo_ch_ ? buflo_ch_->useful_recv_byte_count() : 0;
    return useful_recv_byte_count_so_far_ + from_channel;
}

const uint32_t
ClientSideProxy::num_whole_dummy_cells_dropped_so_far() const
{
    const auto from_channel = buflo_ch_ ? buflo_ch_->num_whole_dummy_cells_dropped() : 0;
    return num_whole_dummy_cells_dropped_so_far_ + from_channel;
}

/*
 * this should be called before about to destroy the buflo channel, so
 * that we can grab its stats
 */
void
ClientSideProxy::_update_stats()
{
    if (buflo_ch_) {
        all_recv_byte_count_so_far_ += buflo_ch_->all_recv_byte_count();
        useful_recv_byte_count_so_far_ += buflo_ch_->useful_recv_byte_count();
        num_whole_dummy_cells_dropped_so_far_ += buflo_ch_->num_whole_dummy_cells_dropped();
    }
}

void
ClientSideProxy::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    vlogself(2) << "a new proxy client";

    if (state_ != State::READY) {
        logself(WARNING) << "reject client traffic since the we're not ready";
        vlogself(2) << "client channel: " << channel->objId();
        // don't need to close the "channel" because it's a unique
        // pointer so it will auto clean up when we return
        return;
    }

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
        state_ = State::CONNECTED;
        _on_connected_to_ssp();
    }
    else {
        logself(FATAL) << "unexpected state " << common::as_integer(state_);
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
        state_ = State::CONNECTED;

        _on_connected_to_ssp();

        break;
    }

    case Socks5ConnectorObserver::ConnectResult::ERR_FAIL:
        logself(FATAL) << "to implement";
        break;

    default:
        logself(FATAL) << "invalid result " << common::as_integer(result);
        break;
    }
}

void
ClientSideProxy::_on_connected_to_ssp()
{
    const auto peer_fd = peer_channel_->release_fd();
    CHECK_GT(peer_fd, 0);

    peer_channel_.reset();

    CHECK_EQ(state_, State::CONNECTED);

    logself(INFO) << "... connected to ssp at transport level";

    buflo_ch_.reset(
        new BufloMuxChannelImplSpdy(
            evbase_, peer_fd, true, myaddr_, 750, buflo_frequencyMs_, buflo_L_,
            buflo_time_limit_secs_,
            boost::bind(&ClientSideProxy::_on_buflo_channel_status,
                        this, _1, _2),
            NULL
            ));
    CHECK_NOTNULL(buflo_ch_.get());

    state_ = State::SETTING_UP_BUFLO_CHANNEL;
}

void
ClientSideProxy::onConnectError(StreamChannel* ch, int errorcode) noexcept
{
    LOG(FATAL) << "error connecting to SSP (or socks proxy): ["
               << evutil_socket_error_to_string(errorcode) << "]";
}

void
ClientSideProxy::onConnectTimeout(StreamChannel*) noexcept
{
    LOG(FATAL) << "timed out connecting to SSP (or socks proxy)";
}

void
ClientSideProxy::start_accepting_clients()
{
    if (!stream_server_->is_listening()) {
        auto rv = stream_server_->start_listening();
        CHECK(rv);
    }

    if (!stream_server_->is_accepting()) {
        auto rv = stream_server_->start_accepting();
        CHECK(rv);
    }
}

void
ClientSideProxy::_on_buflo_channel_status(BufloMuxChannel*,
                                          BufloMuxChannel::ChannelStatus status)
{
    if (status == BufloMuxChannel::ChannelStatus::READY) {
        DestructorGuard dg(this);

        logself(INFO) << "channel to ssp is ready";

        // now we can start accepting client connections
        stream_server_->set_observer(this);

        state_ = State::READY;

        csp_status_cb_(this, true);

    } else if (status == BufloMuxChannel::ChannelStatus::CLOSED) {
        _update_stats();

#ifdef IN_SHADOW

        LOG(WARNING) << "buflo channel is closed, so we're resetting to "
                     << "initial state and then notify user";
        _reset_to_initial();

        csp_status_cb_(this, false);

#else
        LOG(FATAL) << "buflo channel is closed, so we're exiting";
#endif

    } else {
        LOG(FATAL) << "unknown channel status";
    }
}

void
ClientSideProxy::_reset_to_initial()
{
    /* note that this calls destructors of the client handler, and
     * those destructors used to notify our _on_client_handler_done()
     * which will retry to delete the handler again causing double
     * free. the fix we have employed is to make the handler not
     * notify us when they're being destroyed. but to be safe, we also
     * swap the client_handlers_ map with a local here that we clear,
     * so when notified later, our client_handlers_ is empty and so we
     * won't double free the handler */
    decltype(client_handlers_) tmp;
    std::swap(tmp, client_handlers_);
    CHECK(client_handlers_.empty());
    tmp.clear();

    peer_channel_.reset();
    socks_connector_.reset();
    buflo_ch_.reset();

    /* we'll just keep accepting. when clients connect we'll
     * immediately close if we're not ready */
    // stream_server_->pause_accepting();
    state_ = State::INITIAL;
}

void
ClientSideProxy::_on_client_handler_done(ClientHandler* chandler)
{
    vlogself(2) << "chandler done; remove it";
    client_handlers_.erase(chandler->objId());
}

void
ClientSideProxy::_schedule_log_timer()
{
    static const uint64_t interval_ms = 30*1000; // 30 seconds
    const auto current_time_ms = common::gettimeofdayMs();

    // compute delay so that we'll log when current time is a multiple
    // of interval
    const auto delay_ms = (interval_ms - ((current_time_ms) % interval_ms));
    CHECK(delay_ms <= interval_ms) << "bad delay_ms: " << delay_ms;

    log_stats_timer_->cancel();
    log_stats_timer_->start(delay_ms);
}

void
ClientSideProxy::_log_stats_timer_fired(Timer*)
{
    logself(INFO) << "all_recv_byte_count_so_far= " << all_recv_byte_count_so_far()
                  << " useful_recv_byte_count_so_far= " << useful_recv_byte_count_so_far()
                  << " num_whole_dummy_cells_dropped_so_far= " << num_whole_dummy_cells_dropped_so_far();

    _schedule_log_timer();
}

ClientSideProxy::~ClientSideProxy()
{
    LOG(FATAL) << "to do";
}

} // namespace csp
