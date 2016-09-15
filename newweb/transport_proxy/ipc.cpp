
#include <boost/bind.hpp>

#include "ipc.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"
#include "utility/ipc/transport_proxy/gen/combined_headers"


using myio::StreamServer;
using myio::GenericMessageChannel;
using csp::ClientSideProxy;

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
                     ClientSideProxy::UniquePtr csp)
    : evbase_(evbase)
    , stream_server_(std::move(streamserver))
    , csp_(std::move(csp))
{
    stream_server_->set_observer(this);
    vlogself(2) << "tell stream server to start accepting";
    stream_server_->start_accepting();

    csp_->kickstart(boost::bind(&IPCServer::_on_csp_ready, this, _1));
}

void
IPCServer::_on_csp_ready(ClientSideProxy*)
{
    // csp_->start_defense_session(20, 5);
}

void
IPCServer::_handle_StartDefenseSession(const msgs::StartDefenseSessionMsg* msg)
{
    vlogself(2) << "start defense session";

    csp_->start_defense_session(msg->frequencyMs(),
                                msg->durationSec());
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

    GenericMessageChannel::UniquePtr ch(
        new GenericMessageChannel(std::move(channel), this));

    // use the id of the generic msg channel, not the stream channel,
    // as the routing id
    const auto routing_id = ch->objId();

    auto ret = client_channels_.insert(make_pair(routing_id, std::move(ch)));
    CHECK(ret.second); // insist it was newly inserted
}

#define IPC_MSG_HANDLER(TYPE)                                           \
    case msgs::type_ ## TYPE: {                                         \
        _handle_ ## TYPE(msgs::Get ## TYPE ## Msg(data));               \
    }                                                                   \
    break;

void
IPCServer::onRecvMsg(GenericMessageChannel* channel, uint8_t type,
                     uint16_t len, const uint8_t* data) noexcept
{
    const auto routing_id = channel->objId();

    vlogself(2) << "recv'ed msg type= " << msgs::EnumNametype((msgs::type)type)
                << ", len= " << len << ", from channel= " << routing_id;

    switch (type) {

        // IPC_MSG_HANDLER(Hello)
        IPC_MSG_HANDLER(StartDefenseSession)

    default:
        CHECK(false) << "invalid IPC message type " << type;
        break;
    }

}

void
IPCServer::onEOF(GenericMessageChannel* ch) noexcept
{
    const auto routing_id = ch->objId();
    vlogself(2) << "ipc server client stream " << ch << " eof";
}

void
IPCServer::onError(GenericMessageChannel* ch, int errorcode) noexcept
{
    const auto routing_id = ch->objId();
    // CHECK(inMap(client_channels_, routing_id));
    logself(WARNING) << "ipc server client stream " << ch << " error: "
                 << strerror(errorcode);
}

void
IPCServer::onAcceptError(StreamServer*, int errorcode) noexcept
{
    logself(WARNING) << "IPC server has accept error: " << strerror(errorcode);
}
