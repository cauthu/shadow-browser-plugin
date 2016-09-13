

#include <boost/bind.hpp>

#include "../../utility/tcp_server.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/buflo_mux_channel_impl_spdy.hpp"

#include "ssp.hpp"


#define _LOG_PREFIX(inst) << "sstp= " << (inst)->objId() << ": "

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


ServerSideProxy::ServerSideProxy(struct event_base* evbase,
                                 StreamServer::UniquePtr streamserver)
    : evbase_(evbase)
    , stream_server_(std::move(streamserver))
{
    stream_server_->set_observer(this);
    const auto rv = stream_server_->start_accepting();
    CHECK(rv);
}

void
ServerSideProxy::_on_csp_handler_done(CSPHandler* chandler)
{
    csp_handlers_.erase(chandler->objId());
}

void
ServerSideProxy::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    vlogself(2) << "a new csp";

    CSPHandler::UniquePtr chandler(
        new CSPHandler(evbase_,
                       std::move(channel),
                       boost::bind(&ServerSideProxy::_on_csp_handler_done,
                                   this, _1)));
    const auto chid = chandler->objId();
    const auto ret = csp_handlers_.insert(
        make_pair(chid, std::move(chandler)));
    CHECK(ret.second); // insist it was newly inserted
}

void
ServerSideProxy::onAcceptError(StreamServer*, int errorcode) noexcept
{
    LOG(WARNING) << "ServerSideProxy has accept error: " << strerror(errorcode);
}

ServerSideProxy::~ServerSideProxy()
{
    LOG(FATAL) << "not reached";
}
