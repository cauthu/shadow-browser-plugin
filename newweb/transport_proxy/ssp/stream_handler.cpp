
#include <boost/bind.hpp>

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
                             StreamHandlerDoneCb handler_done_cb)
    : evbase_(evbase)
    , buflo_channel_(buflo_ch)
    , sid_(sid)
    , handler_done_cb_(handler_done_cb)
{
    buflo_channel_->set_stream_observer(sid_, this);

    const auto addr = common::getaddr(target_host);

    struct timeval timeout = {0};
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    target_channel_.reset(
        new TCPChannel(evbase_, addr, port, nullptr));
    auto rv = target_channel_->start_connecting(this, &timeout);
    CHECK_EQ(rv, 0);

    state_ = State::CONNECTING_TARGET;
}

void
StreamHandler::onConnected(StreamChannel*) noexcept
{
    CHECK_EQ(state_, State::CONNECTING_TARGET);

    vlogself(2) << "connected to target";
    if (buflo_channel_->set_stream_connected(sid_)) {
        vlogself(2) << "linked!";
        state_ = State::FORWARDING;

        // hand off the the two streams to inner outer handler to do
        // the forwarding
        inner_outer_handler_.reset(
            new InnerOuterHandler(
                target_channel_.get(), sid_, buflo_channel_,
                boost::bind(&StreamHandler::_on_inner_outer_handler_done,
                            this, _1, _2)));
        buflo_channel_ = nullptr;
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
    logself(WARNING) << "error connecting to target";
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
StreamHandler::onStreamNewDataAvailable(BufloMuxChannel*) noexcept
{
    logself(FATAL) << "not reached";
}

void
StreamHandler::onStreamClosed(BufloMuxChannel*) noexcept
{
    // we should only get this stream closed when we're still trying
    // to connect to the target; after that, the innerouterhandler
    // should be the one getting called by the stream
    CHECK_EQ(state_, State::CONNECTING_TARGET);
    buflo_channel_ = nullptr;
    _close();
}

void
StreamHandler::_close()
{
    vlogself(2) << "begin";

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

    vlogself(2) << "done";
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
