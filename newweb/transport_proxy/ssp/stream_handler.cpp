
#include "stream_handler.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/tcp_channel.hpp"



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


StreamHandler::StreamHandler(struct event_base* evbase,
                             BufloMuxChannel* buflo_ch,
                             const int sid,
                             const char* target_host,
                             const uint16_t& port,
                             StreamHandlerDoneCb handler_done_cb)
    : evbase_(evbase)
    , buflo_ch_(buflo_ch)
    , sid_(sid)
    , handler_done_cb_(handler_done_cb)
{
    buflo_ch_->set_stream_observer(sid_, this);

    const auto addr = common::getaddr(target_host);

    target_channel_.reset(
        new TCPChannel(evbase_, addr, port, this));
    auto rv = target_channel_->start_connecting(this);
    CHECK_EQ(rv, 0);

    state_ = State::CONNECTING;
}

void
StreamHandler::onConnected(StreamChannel*) noexcept
{
    vlogself(2) << "connected to target!";
    if (buflo_ch_->set_stream_connect_result(sid_, true)) {
        vlogself(2) << "linked!";
        state_ = State::LINKED;
    } else {
        // some error
        vlogself(2) << "some error!";
        _close(true);
    }
}

void
StreamHandler::onConnectError(StreamChannel*, int) noexcept
{
    buflo_ch_->set_stream_connect_result(sid_, false);
    _close(true);
}

void
StreamHandler::onConnectTimeout(StreamChannel*) noexcept
{
    buflo_ch_->set_stream_connect_result(sid_, false);
    _close(true);
}

void
StreamHandler::onStreamNewDataAvailable(myio::buflo::BufloMuxChannel*)
{}

void
StreamHandler::onStreamClosed(myio::buflo::BufloMuxChannel*)
{}

void
StreamHandler::onNewReadDataAvailable(myio::StreamChannel*) noexcept
{}

void
StreamHandler::onWrittenData(myio::StreamChannel*) noexcept
{}

void
StreamHandler::onEOF(myio::StreamChannel*) noexcept
{}

void
StreamHandler::onError(myio::StreamChannel*, int errorcode) noexcept
{}

void
StreamHandler::_close(const bool do_notify)
{
    if (state_ == State::CLOSED) {
        return;
    }

    state_ = State::CLOSED;

    // close the stream
    buflo_ch_->close_stream(sid_);

    target_channel_.reset();

    if (do_notify) {
        DestructorGuard dg(this);
        handler_done_cb_(this);
    }
}

StreamHandler::~StreamHandler()
{
    // assume that we are being deleted by csp handler and so don't
    // need to notify it
    _close(false);
}
