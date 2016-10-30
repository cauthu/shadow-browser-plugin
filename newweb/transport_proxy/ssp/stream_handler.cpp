
#include <boost/bind.hpp>
#include <fstream>      // std::ofstream

#include "stream_handler.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/tcp_channel.hpp"
#include "../../utility/buflo_mux_channel_impl_spdy.hpp"



#define _LOG_PREFIX(inst) << "shandler= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


using myio::StreamChannel;
using myio::TCPChannel;
using myio::buflo::BufloMuxChannel;
using myio::buflo::BufloMuxChannelImplSpdy;


namespace ssp
{

StreamHandler::StreamHandler(struct event_base* evbase,
                             BufloMuxChannel* buflo_ch,
                             const int sid,
                             const char* target_host,
                             const uint16_t& port,
                             const bool& log_connect_latency,
                             StreamHandlerDoneCb handler_done_cb)
    : evbase_(evbase)
    , buflo_channel_(buflo_ch)
    , sid_(sid)
    , handler_done_cb_(handler_done_cb)
    , target_host_(target_host)
    , target_port_(port)
{
    CHECK_GT(sid, -1);

    buflo_channel_->set_stream_observer(sid_, this);

    vlogself(2) << "stream handler will connect to target ["
                << target_host << "]:" << port;

    uint64_t resolve_start_time_ms = 0;
    if (log_connect_latency) {
        resolve_start_time_ms = common::gettimeofdayMs();
    }

    const auto addr = common::getaddr(target_host);

    if (log_connect_latency) {
        const auto resolv_done_time = common::gettimeofdayMs();
        CHECK(resolv_done_time >= resolve_start_time_ms);
        std::ofstream ofs;
        ofs.open("outer_connect_latencies.txt", std::ofstream::out | std::ofstream::app);
        ofs << target_host_ << " resolve "
            << (resolv_done_time - resolve_start_time_ms) << " ms\n";
    }

    struct timeval timeout = {0};
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    target_channel_.reset(
        new TCPChannel(evbase_, addr, port, nullptr));
    auto rv = target_channel_->start_connecting(this, &timeout);
    CHECK_EQ(rv, 0);

    if (log_connect_latency) {
        connect_start_time_ms_ = common::gettimeofdayMs();
        CHECK(connect_start_time_ms_ > 0);
    }

    state_ = State::CONNECTING_TARGET;
}

void
StreamHandler::onConnected(StreamChannel*) noexcept
{
    CHECK_EQ(state_, State::CONNECTING_TARGET);

    vlogself(2) << "connected to target";
    if (buflo_channel_->set_stream_connected(sid_)) {
        if (connect_start_time_ms_) {
            auto const connect_done_time = common::gettimeofdayMs();
            CHECK(connect_done_time >= connect_start_time_ms_);
            std::ofstream ofs;
            ofs.open("outer_connect_latencies.txt", std::ofstream::out | std::ofstream::app);
            ofs << target_host_ << " connect "
                << (connect_done_time - connect_start_time_ms_) << " ms\n";
        }

        vlogself(2) << "linked!";
        state_ = State::FORWARDING;

        // hand off the the two streams to inner outer handler to do
        // the forwarding
        inner_outer_handler_.reset(
            new InnerOuterHandler(
                target_channel_.get(), sid_, buflo_channel_,
                boost::bind(&StreamHandler::_on_inner_outer_handler_done,
                            this, _1, _2)));

        /* we need to hang on the buflo_channel_ so that if the inner
         * outer handler tells us the outer stream has closed, then we
         * can close the inner stream
         */
        // buflo_channel_ = nullptr;
    } else {
        // some error
        vlogself(2) << "some error";
        _close();
    }
}

void
StreamHandler::onConnectError(StreamChannel*, int) noexcept
{
    CHECK_EQ(state_, State::CONNECTING_TARGET);
    logself(ERROR) << "error connecting to target [" << target_host_ << "]:"
                   << target_port_;
    _close();
}

void
StreamHandler::onConnectTimeout(StreamChannel*) noexcept
{
    CHECK_EQ(state_, State::CONNECTING_TARGET);
    logself(WARNING) << "times out connecting to target";
    _close();
}

void
StreamHandler::_on_inner_outer_handler_done(InnerOuterHandler*,
                                            bool inner_stream_already_closed)
{
    CHECK_EQ(state_, State::FORWARDING);
    if (inner_stream_already_closed) {
        buflo_channel_ = nullptr;
    }
    _close();
}

void
StreamHandler::onStreamNewDataAvailable(BufloMuxChannel*, int) noexcept
{
    logself(FATAL) << "not reached";
}

void
StreamHandler::onStreamClosed(BufloMuxChannel*, int) noexcept
{
    // we should only get this stream closed when we're still trying
    // to connect to the target (after that, the innerouterhandler
    // should be the one getting called by the stream), therefore we
    // can clear buflo_channel_ because we won't need to tell it to
    // close the stream
    CHECK_EQ(state_, State::CONNECTING_TARGET);
    buflo_channel_ = nullptr;
    _close();
}

void
StreamHandler::_close()
{
    /* avoid logging in this function because it can be called by
     * destructor and we're seeing valgrind report "invalid read" at
     * the last log statement
     */

    // vlogself(2) << "begin";

    if (state_ != State::CLOSED) {
        state_ = State::CLOSED;

        target_channel_.reset();

        if (buflo_channel_) {
            buflo_channel_->set_stream_observer(sid_, nullptr);
            buflo_channel_->close_stream(sid_);
            buflo_channel_ = nullptr;
        }

        if (handler_done_cb_) {
            DestructorGuard dg(this);
            handler_done_cb_(this);
            handler_done_cb_ = NULL;
        }
    }

    // vlogself(2) << "done";
}

StreamHandler::~StreamHandler()
{
    vlogself(2) << "streamhandler destructing";
    // currently we expect only the CSPHandler deletes us (e.g., on
    // its destructor or when it tears down the buflo tunnel), so we
    // don't need to notify it (because it might free us again),
    // resulting in double free
    handler_done_cb_ = NULL;
    _close();
}

}
