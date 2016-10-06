
#include <boost/bind.hpp>

#include "ipc.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"
#include "../utility/folly/ScopeGuard.h"

#include "utility/ipc/transport_proxy/gen/combined_headers"


using myio::StreamServer;
using myipc::GenericIpcChannel;
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
    , establish_tunnel_call_id_(0)
{
    stream_server_->set_observer(this);
    vlogself(2) << "ipc server starts accepting";
    stream_server_->start_accepting();
}


#undef BEGIN_BUILD_MSG_AND_SEND_AT_END
#define BEGIN_BUILD_MSG_AND_SEND_AT_END(TYPE, bufbuilder)               \
    auto const __type = msgs::type_ ## TYPE;                            \
    VLOG(2) << "begin building msg type: " << __type;                   \
    msgs::TYPE ## MsgBuilder msgbuilder(bufbuilder);                    \
    SCOPE_EXIT {                                                        \
        auto msg = msgbuilder.Finish();                                 \
        bufbuilder.Finish(msg);                                         \
        VLOG(2) << "send msg type: " << __type;                         \
        ipc_client_channel_->sendMsg(                                   \
            __type, bufbuilder.GetSize(),                               \
            bufbuilder.GetBufferPointer());                             \
    }

#define BEGIN_BUILD_RESP_MSG_AND_SEND_AT_END(TYPE, bufbuilder, id)      \
    auto const __type = msgs::type_ ## TYPE;                            \
    VLOG(2) << "begin building msg type: " << __type;                   \
    msgs::TYPE ## MsgBuilder msgbuilder(bufbuilder);                    \
    SCOPE_EXIT {                                                        \
        auto msg = msgbuilder.Finish();                                 \
        bufbuilder.Finish(msg);                                         \
        VLOG(2) << "send msg type: " << __type;                         \
        ipc_client_channel_->reply(                                     \
            id, __type, bufbuilder.GetSize(),                           \
            bufbuilder.GetBufferPointer());                             \
    }

void
IPCServer::_on_csp_ready(ClientSideProxy*)
{
    CHECK_GT(establish_tunnel_call_id_, 0);

    {
        // send the response
        flatbuffers::FlatBufferBuilder bufbuilder;
        const auto id = establish_tunnel_call_id_;

        BEGIN_BUILD_RESP_MSG_AND_SEND_AT_END(
            EstablishTunnelResp, bufbuilder, id);
        msgbuilder.add_tunnelIsReady(true);
        msgbuilder.add_allRecvByteCountSoFar(csp_->all_recv_byte_count_so_far());
        msgbuilder.add_usefulRecvByteCountSoFar(csp_->useful_recv_byte_count_so_far());
    }

    establish_tunnel_call_id_ = 0;
}

void
IPCServer::_handle_EstablishTunnel(const uint32_t& id,
                                   const msgs::EstablishTunnelMsg* msg)
{
    vlogself(2) << "tell csp to establish tunnel";

    CHECK_GT(id, 0);
    CHECK_EQ(establish_tunnel_call_id_, 0);
    establish_tunnel_call_id_ = id;

    const auto rv = csp_->establish_tunnel(
        boost::bind(&IPCServer::_on_csp_ready, this, _1),
        msg->forceReconnect());
    CHECK_EQ(rv, ClientSideProxy::EstablishReturnValue::PENDING);
}

void
IPCServer::_handle_SetAutoStartDefenseOnNextSend(const uint32_t& id,
                                                 const msgs::SetAutoStartDefenseOnNextSendMsg* msg)
{
    CHECK_GT(id, 0);

    csp_->set_auto_start_defense_session_on_next_send();

    {
        // send the response
        flatbuffers::FlatBufferBuilder bufbuilder;
        BEGIN_BUILD_RESP_MSG_AND_SEND_AT_END(
            SetAutoStartDefenseOnNextSendResp, bufbuilder, id);
        msgbuilder.add_ok(true);
    }
}

void
IPCServer::_handle_StopDefense(const uint32_t& id,
                               const msgs::StopDefenseMsg* msg)
{
    logself(INFO) << "request to stop buflo defense";

    CHECK_GT(id, 0);

    csp_->stop_defense_session(msg->right_now());

    {
        // send the response
        flatbuffers::FlatBufferBuilder bufbuilder;
        BEGIN_BUILD_RESP_MSG_AND_SEND_AT_END(
            StopDefenseResp, bufbuilder, id);
    }
}

void
IPCServer::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    // accepted an ipc client
    _setup_client(std::move(channel));
}

void
IPCServer::onAcceptError(StreamServer*, int errorcode) noexcept
{
    logself(WARNING) << "IPC server has accept error: " << strerror(errorcode);
}

void
IPCServer::_setup_client(StreamChannel::UniquePtr channel)
{
    vlogself(2) << "ipc server got new client";

    // supports only one ipc client at a time
    CHECK(!ipc_client_channel_.get());
    ipc_client_channel_.reset(
        new GenericIpcChannel(
            evbase_,
            std::move(channel),
            boost::bind(&IPCServer::_on_msg, this, _2, _3, _4),
            boost::bind(&IPCServer::_on_called, this, _2, _3, _4, _5),
            boost::bind(&IPCServer::_on_client_channel_status, this, _2)));
}

#define IPC_MSG_HANDLER(TYPE)                                           \
    case msgs::type_ ## TYPE: {                                         \
        _handle_ ## TYPE(msgs::Get ## TYPE ## Msg(data));               \
    }                                                                   \
    break;

void
IPCServer::_on_msg(uint8_t type,
                   uint16_t len, const uint8_t* data)
{
    logself(FATAL) << "to do";
}

#define IPC_CALL_HANDLER(TYPE)                                          \
    case msgs::type_ ## TYPE: {                                         \
        _handle_ ## TYPE(id, msgs::Get ## TYPE ## Msg(data));           \
    }                                                                   \
    break;

void
IPCServer::_on_called(uint32_t id, uint8_t type,
                      uint16_t len, const uint8_t* data)
{
    switch (type) {

        IPC_CALL_HANDLER(EstablishTunnel)
        IPC_CALL_HANDLER(SetAutoStartDefenseOnNextSend)
        IPC_CALL_HANDLER(StopDefense)

    default:
        CHECK(false) << "invalid IPC message type " << unsigned(type);
        break;
    }

}

void
IPCServer::_on_client_channel_status(GenericIpcChannel::ChannelStatus status)
{
    logself(FATAL) << "to do";
}

#undef BEGIN_BUILD_MSG_AND_SEND_AT_END
