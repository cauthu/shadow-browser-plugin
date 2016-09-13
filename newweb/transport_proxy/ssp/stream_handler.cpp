
#include "stream_handler.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/tcp_channel.hpp"

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
    if (buflo_ch_->set_stream_connect_result(sid_, true)) {
        state_ = State::LINKED;
    } else {
        // some error
        DestructorGuard dg(this);
        handler_done_cb_(this);
    }
}

void
StreamHandler::onConnectError(StreamChannel*, int) noexcept
{}

void
StreamHandler::onConnectTimeout(StreamChannel*) noexcept
{}

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
