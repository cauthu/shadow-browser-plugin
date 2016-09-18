
#include <boost/bind.hpp>

#include "ipc.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "utility/ipc/io_service/gen/combined_headers"

#include "http_session.hpp"


using myio::StreamServer;
using myipc::GenericIpcChannel;

namespace msgs = myipc::ioservice::messages;
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
IPCServer::_handle_Hello(const uint32_t& routing_id, const msgs::HelloMsg* msg)
{
    vlogself(2) << "received HelloMsg: " << msg->resId() << ", " << msg->xyz();
}

void
IPCServer::_handle_Fetch(const uint32_t& routing_id, const msgs::FetchMsg* msg)
{
    vlogself(2) << "begin handling FetchMsg";

    hsessions_[routing_id]->handle_Fetch(msg);
}

void
IPCServer::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    _setup_client(std::move(channel));
}

void
IPCServer::_setup_client(StreamChannel::UniquePtr channel)
{
    vlogself(2) << "ipc server got new client";

    GenericIpcChannel::UniquePtr ch(
        new GenericIpcChannel(
            evbase_,
            std::move(channel),
            boost::bind(&IPCServer::_on_msg_recv, this, _1, _2, _3, _4),
            boost::bind(&IPCServer::_on_called, this, _1, _2, _3, _4, _5),
            boost::bind(&IPCServer::_on_channel_status, this, _1, _2)));

    // use the id of the generic msg channel, not the stream channel,
    // as the routing id
    const auto routing_id = ch->objId();

    auto ret = client_channels_.insert(make_pair(routing_id, std::move(ch)));
    CHECK(ret.second); // insist it was newly inserted

    // for now each client will get its own session
    shared_ptr<HttpNetworkSession> hsess(
        new HttpNetworkSession(evbase_, this, routing_id, netconf_),
        [](HttpNetworkSession* s) { s->destroy(); });
    DCHECK_NOTNULL(hsess.get());

    auto ret2 = hsessions_.insert(make_pair(routing_id, hsess));
    CHECK(ret2.second); // insist it was newly inserted
}

#define IPC_MSG_HANDLER(TYPE)                                           \
    case msgs::type_ ## TYPE: {                                         \
        _handle_ ## TYPE(routing_id, msgs::Get ## TYPE ## Msg(buf));   \
    }                                                                   \
    break;

void
IPCServer::_on_msg_recv(GenericIpcChannel* channel, uint8_t type,
                        uint16_t len, const uint8_t* buf)
{
    const auto routing_id = channel->objId();

    vlogself(2) << "recv'ed msg type= " << msgs::EnumNametype((msgs::type)type)
                << ", len= " << len << ", from channel= " << routing_id;

    switch (type) {

        IPC_MSG_HANDLER(Hello)
        IPC_MSG_HANDLER(Fetch)

    default:
        logself(FATAL) << "invalid IPC message type " << type;
        break;
    }

}

void
IPCServer::_on_called(GenericIpcChannel*, uint32_t id, uint8_t type,
                        uint16_t len, const uint8_t* buf)
{
    logself(FATAL) << "to do";
}

void
IPCServer::_on_channel_status(GenericIpcChannel* ch,
                              GenericIpcChannel::ChannelStatus status)
{
    const auto routing_id = ch->objId();
    vlogself(2) << "ipc client stream " << ch << " closed";
    _remove_route(routing_id);
}

void
IPCServer::_remove_route(const uint32_t& routing_id)
{
    client_channels_.erase(routing_id);
    hsessions_.erase(routing_id);
}

void
IPCServer::onAcceptError(StreamServer*, int errorcode) noexcept
{
    logself(WARNING) << "IPC server has accept error: " << strerror(errorcode);
}
