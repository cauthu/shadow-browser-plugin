
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


InnerOuterHandler::InnerOuterHandler(StreamChannel* outer_channel,
                                     int inner_sid,
                                     BufloMuxChannel* buflo_ch,
                                     InnerOuterHandlerDoneCb handler_done_cb)
    : outer_channel_(outer_channel)
    , buflo_channel_(buflo_ch)
    , inner_sid_(inner_sid)
    , handler_done_cb_(handler_done_cb)
{
    vlogself(2) << "outer ch: " << outer_channel_->objId()
                << " inner st id: " << inner_sid_
                << " buflo mux ch: " << buflo_channel_->objId();
    outer_channel_->set_observer(this);
    buflo_channel_->set_stream_observer(inner_sid_, this);

    CHECK_EQ(outer_channel_->get_avail_input_length(), 0);
    CHECK_EQ(buflo_channel_->get_avail_input_length(inner_sid_), 0);
}

void
InnerOuterHandler::onStreamNewDataAvailable(myio::buflo::BufloMuxChannel*) noexcept
{
    vlogself(2) << "copy data inner --> outer";
    auto buf = buflo_channel_->get_input_evbuf(inner_sid_);
    auto rv = outer_channel_->write_buffer(buf);
    CHECK_EQ(rv, 0);
    vlogself(2) << "done";
}

void
InnerOuterHandler::onStreamClosed(myio::buflo::BufloMuxChannel*) noexcept
{
    _be_done(true);
}

void
InnerOuterHandler::onNewReadDataAvailable(myio::StreamChannel*) noexcept
{
    vlogself(2) << "copy data inner <-- outer";
    auto buf = outer_channel_->get_input_evbuf();
    auto rv = buflo_channel_->write_buffer(inner_sid_, buf);
    CHECK_EQ(rv, 0);
    vlogself(2) << "done";
}

void
InnerOuterHandler::onWrittenData(myio::StreamChannel*) noexcept
{
    // don't care
}

void
InnerOuterHandler::onEOF(myio::StreamChannel*) noexcept
{
    vlogself(2) << "outer stream EOF";
    _be_done();
}

void
InnerOuterHandler::onError(myio::StreamChannel*, int errorcode) noexcept
{
    vlogself(2) << "outer stream error";
    _be_done();
}

void
InnerOuterHandler::_be_done(bool inner_stream_already_closed)
{
    DestructorGuard dg(this);
    handler_done_cb_(this, inner_stream_already_closed);
}
