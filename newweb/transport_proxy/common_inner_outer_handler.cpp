
#include "common_inner_outer_handler.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"



#define _LOG_PREFIX(inst) << "inouhandler= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


using myio::StreamChannel;
using myio::buflo::BufloMuxChannel;


InnerOuterHandler::InnerOuterHandler(StreamChannel::UniquePtr outer_channel,
                                     int inner_sid,
                                     BufloMuxChannel* buflo_ch,
                                     InnerOuterHandlerDoneCb handler_done_cb)
    : outer_channel_(std::move(outer_channel))
    , buflo_channel_(buflo_ch)
    , inner_sid_(inner_sid)
    , handler_done_cb_(handler_done_cb)
{
    vlogself(2) << "outer ch: " << outer_channel_->objId()
                << " inner st id: " << inner_sid_
                << " buflo mux ch: " << buflo_channel_->objId();
    outer_channel_->set_observer(this);
    buflo_channel_->set_stream_observer(inner_sid_, this);
}

void
InnerOuterHandler::onStreamNewDataAvailable(myio::buflo::BufloMuxChannel*) noexcept
{
    logself(FATAL) << "to do";
}

void
InnerOuterHandler::onStreamClosed(myio::buflo::BufloMuxChannel*) noexcept
{
    _close(true, false);
}

void
InnerOuterHandler::onNewReadDataAvailable(myio::StreamChannel*) noexcept
{

}

void
InnerOuterHandler::onWrittenData(myio::StreamChannel*) noexcept
{

}

void
InnerOuterHandler::onEOF(myio::StreamChannel*) noexcept
{

    _close(true, true);
}

void
InnerOuterHandler::onError(myio::StreamChannel*, int errorcode) noexcept
{

    _close(true, true);
}

void
InnerOuterHandler::_close(const bool& notify_handler_done,
                          const bool& close_buflo_stream)
{
    outer_channel_.reset();

    if (close_buflo_stream && buflo_channel_) {
        buflo_channel_->set_stream_observer(inner_sid_, nullptr);
        buflo_channel_->close_stream(inner_sid_);
    }

    buflo_channel_ = nullptr;

    if (notify_handler_done) {
        DestructorGuard dg(this);
        handler_done_cb_(this);
    }
}

InnerOuterHandler::~InnerOuterHandler()
{
    vlogself(2) << "streamhandler destructing";
    // assume that we are being deleted by csp handler and so don't
    // need to notify it
    _close(false, true);
}
