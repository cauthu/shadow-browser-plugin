
#include <boost/bind.hpp>

#include "csp_handler.hpp"
#include "stream_handler.hpp"
#include "../../utility/buflo_mux_channel_impl_spdy.hpp"


#define _LOG_PREFIX(inst) << "csphandler= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


using std::make_pair;
using myio::StreamChannel;
using myio::buflo::BufloMuxChannel;
using myio::buflo::BufloMuxChannelImplSpdy;


namespace ssp
{

CSPHandler::CSPHandler(struct event_base* evbase,
                       const uint32_t& tamaraw_pkt_intvl_ms,
                       const uint32_t& tamaraw_L,
                       StreamChannel::UniquePtr csp_channel,
                       CSPHandlerDoneCb handler_done_cb)
    : evbase_(evbase)
    , handler_done_cb_(handler_done_cb)
{
    const auto fd = csp_channel->release_fd();
    csp_channel.reset();

    buflo_channel_.reset(
        new BufloMuxChannelImplSpdy(
            evbase, fd, false, 750, tamaraw_pkt_intvl_ms, tamaraw_L,
            boost::bind(&CSPHandler::_on_buflo_channel_status,
                        this, _1, _2),
            boost::bind(&CSPHandler::_on_buflo_new_stream_connect_request,
                        this, _1, _2, _3, _4)
            ));
}

void
CSPHandler::_on_buflo_channel_status(BufloMuxChannel*,
                                     BufloMuxChannel::ChannelStatus status)
{
    // we are on ssp, so we currently don't need to do anything about
    // the ::READY status

    if (status == BufloMuxChannel::ChannelStatus::CLOSED) {
        DestructorGuard dg(this);
        handler_done_cb_(this);
    }
}

void
CSPHandler::_on_buflo_new_stream_connect_request(
    BufloMuxChannel*, int sid,
    const char* host, uint16_t port)
{
    // hand off the stream to handler

    StreamHandler::UniquePtr shandler(
        new StreamHandler(
            evbase_, buflo_channel_.get(), sid, host, port,
            boost::bind(&CSPHandler::_on_stream_handler_done, this, _1)));
    const auto shid = shandler->objId();
    const auto ret = shandlers_.insert(
        make_pair(shid, std::move(shandler)));
    CHECK(ret.second); // insist it was newly inserted
}

void
CSPHandler::_on_stream_handler_done(StreamHandler* shandler)
{
    const auto shid = shandler->objId();
    shandlers_.erase(shid);
}

}
