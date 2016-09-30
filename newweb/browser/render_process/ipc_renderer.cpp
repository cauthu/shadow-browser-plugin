
#include <boost/bind.hpp>
#include <stdlib.h>     /* srand, rand */

#include "../../utility/easylogging++.h"
#include "../../utility/stream_server.hpp"
#include "../../utility/folly/ScopeGuard.h"
#include "utility/ipc/renderer/gen/combined_headers"
#include "ipc_renderer.hpp"

using myio::StreamChannel;
using myipc::GenericIpcChannel;
using myio::StreamServer;

namespace msgs = myipc::renderer::messages;
using msgs::type;


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
                     blink::Webengine* webengine)
    : evbase_(evbase)
    , stream_server_(std::move(streamserver))
    , webengine_(webengine)
    , load_call_id_(0)
{
    stream_server_->set_observer(this);
    vlogself(2) << "ipc server starts accepting";
    stream_server_->start_accepting();
}


/* send the msg at the END OF CURRENT SCOPE */
#define BEGIN_BUILD_MSG_AND_SEND_AT_END(TYPE, bufbuilder)               \
    auto const __type = msgs::type_ ## TYPE;                            \
    VLOG(2) << "begin building msg type: " << msgs::EnumNametype(__type); \
    msgs::TYPE ## MsgBuilder msgbuilder(bufbuilder);                    \
    SCOPE_EXIT {                                                        \
        auto msg = msgbuilder.Finish();                                 \
        bufbuilder.Finish(msg);                                         \
        VLOG(2) << "send msg";                                          \
        ipc_client_channel_->sendMsg(                                   \
            __type, bufbuilder.GetSize(),                               \
            bufbuilder.GetBufferPointer());                             \
    }

/* send the msg at the END OF CURRENT SCOPE */
#define BEGIN_BUILD_RESP_MSG_AND_SEND_AT_END(TYPE, bufbuilder, id)      \
    auto const __type = msgs::type_ ## TYPE;                            \
    VLOG(2) << "begin building msg type: " << msgs::EnumNametype(__type); \
    msgs::TYPE ## MsgBuilder msgbuilder(bufbuilder);                    \
    SCOPE_EXIT {                                                        \
        auto msg = msgbuilder.Finish();                                 \
        bufbuilder.Finish(msg);                                         \
        VLOG(2) << "send msg";                                          \
        ipc_client_channel_->reply(                                     \
            id, __type, bufbuilder.GetSize(),                           \
            bufbuilder.GetBufferPointer());                             \
    }


void
IPCServer::send_Loaded()
{
    {
        // send the response for the call
        flatbuffers::FlatBufferBuilder bufbuilder;
        BEGIN_BUILD_MSG_AND_SEND_AT_END(Loaded, bufbuilder);
    }
}

void
IPCServer::_handle_Load(const uint32_t& id,
                        const msgs::LoadMsg* msg)
{
    vlogself(2) << "begin, id= " << id;
    CHECK_GT(id, 0);
    CHECK_EQ(load_call_id_, 0) << load_call_id_;
    load_call_id_ = id;

    // io_service_send_request_resource(
    //     3124, "cnn.com", 80, 123, 234, 63000 + (rand() % 1000));

    webengine_->loadPage(msg->model_fpath()->c_str());

    {
        // send the response for the call
        flatbuffers::FlatBufferBuilder bufbuilder;
        BEGIN_BUILD_RESP_MSG_AND_SEND_AT_END(
            LoadResp, bufbuilder, load_call_id_);
    }

    // reset it
    load_call_id_ = 0;
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

        // IPC_MSG_HANDLER(Hello)
        IPC_CALL_HANDLER(Load)

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
