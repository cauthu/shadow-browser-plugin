
#include "ipc.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "utility/ipc/transport_proxy/gen/combined_headers"

#include "handler.hpp"


using myio::StreamServer;
using myio::GenericMessageChannel;

namespace msgs = myipc::transport_proxy::messages;
using msgs::type;

using std::shared_ptr;


#define _LOG_PREFIX(inst) << "ipcserv= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


IPCServer::IPCServer(struct event_base* evbase,
                     StreamServer::UniquePtr streamserver,
                     const NetConfig* netconf)
    : evbase_(evbase)
    , stream_server_(std::move(streamserver))
    , netconf_(netconf)
{
    stream_server_->set_observer(this);
    vlogself(2) << "tell stream server to start accepting";
    stream_server_->start_accepting();
}

void
IPCServer::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    _setup_handler(std::move(channel));
}

void
IPCServer::_setup_handler(StreamChannel::UniquePtr channel)
{
    vlogself(2) << "ipc server got new client";

    GenericMessageChannel::UniquePtr ch(
        new GenericMessageChannel(std::move(channel), this));

    Handler* handler = new Handler(
        evbase_, nullptr, netconf_, true /* client-side */);
    DCHECK_NOTNULL(handler);
}
