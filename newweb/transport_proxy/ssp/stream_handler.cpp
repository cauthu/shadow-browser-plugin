
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


namespace ssp
{

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
        new TCPChannel(evbase_, addr, port, nullptr));
    auto rv = target_channel_->start_connecting(this);
    CHECK_EQ(rv, 0);

    state_ = State::CONNECTING;
}

void
StreamHandler::onConnected(StreamChannel*) noexcept
{
    CHECK_EQ(state_, State::CONNECTING);

    vlogself(2) << "connected to target";
    if (buflo_ch_->set_stream_connect_result(sid_, true)) {
        vlogself(2) << "linked!";
        state_ = State::LINKED;
    } else {
        // some error
        vlogself(2) << "some error";
        _close(true, true);
    }
}

void
StreamHandler::onConnectError(StreamChannel*, int) noexcept
{
    CHECK_EQ(state_, State::CONNECTING);

    // buflo_ch_->set_stream_connect_result(sid_, false);
    _close(true, true);
}

void
StreamHandler::onConnectTimeout(StreamChannel*) noexcept
{
    CHECK_EQ(state_, State::CONNECTING);

    // buflo_ch_->set_stream_connect_result(sid_, false);
    _close(true, true);
}

// void
// StreamHandler::onStreamNewDataAvailable(myio::buflo::BufloMuxChannel*)
// {
//     CHECK_EQ(state_, State::LINKED);

// }

void
StreamHandler::onStreamClosed(myio::buflo::BufloMuxChannel*)
{
    CHECK_EQ(state_, State::LINKED);
    _close(true, false);
}

// void
// StreamHandler::onNewReadDataAvailable(myio::StreamChannel*) noexcept
// {
//     CHECK_EQ(state_, State::LINKED);
// }

// void
// StreamHandler::onWrittenData(myio::StreamChannel*) noexcept
// {
//     CHECK_EQ(state_, State::LINKED);
// }

// void
// StreamHandler::onEOF(myio::StreamChannel*) noexcept
// {
//     CHECK_EQ(state_, State::LINKED);
//     _close(true, true);
// }

// void
// StreamHandler::onError(myio::StreamChannel*, int errorcode) noexcept
// {
//     CHECK_EQ(state_, State::LINKED);
//     _close(true, true);
// }

void
StreamHandler::_close(const bool& notify_handler_done,
                      const bool& close_buflo_stream)
{
    if (state_ == State::CLOSED) {
        return;
    }

    state_ = State::CLOSED;

    target_channel_.reset();

    if (close_buflo_stream) {
        buflo_ch_->set_stream_observer(sid_, nullptr);
        buflo_ch_->close_stream(sid_);
    }

    buflo_ch_ = nullptr;

    if (notify_handler_done) {
        DestructorGuard dg(this);
        handler_done_cb_(this);
    }
}

StreamHandler::~StreamHandler()
{
    vlogself(2) << "streamhandler destructing";
    // assume that we are being deleted by csp handler and so don't
    // need to notify it
    _close(false, true);
}

}
