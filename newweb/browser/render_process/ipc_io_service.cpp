
#include <boost/bind.hpp>

#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"
#include "utility/ipc/io_service/gen/combined_headers"
#include "ipc_io_service.hpp"


using myio::StreamChannel;
using myipc::GenericIpcChannel;


namespace msgs = myipc::ioservice::messages;
using msgs::type;


#define _LOG_PREFIX(inst)

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


IOServiceIPCClient::IOServiceIPCClient(struct event_base* evbase,
                                       StreamChannel::UniquePtr stream_channel,
                                       ChannelStatusCb ch_status_cb)
    : ch_status_cb_(ch_status_cb)
{
    VLOG(2) << "setting up ipc conn to io process";
    gen_ipc_chan_.reset(
        new GenericIpcChannel(
            evbase,
            std::move(stream_channel),
            boost::bind(&IOServiceIPCClient::_on_msg, this, _1, _2, _3, _4),
            boost::bind(&IOServiceIPCClient::_on_channel_status, this, _1, _2)));
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
        gen_ipc_chan_->sendMsg(                                         \
            __type, bufbuilder.GetSize(),                               \
            bufbuilder.GetBufferPointer());                             \
    }

#define IPC_MSG_HANDLER(TYPE)                                           \
    case msgs::type_ ## TYPE: {                                         \
        _handle_ ## TYPE(msgs::Get ## TYPE ## Msg(data));               \
    }                                                                   \
    break;

void
IOServiceIPCClient::request_resource(const int& req_id,
                                     const uint32_t& resInstNum,
                                     const char* host,
                          const uint16_t& port,
                          const size_t& req_total_size,
                          const size_t& resp_meta_size,
                          const size_t& resp_body_size)
{
    vlogself(2) << "begin, [" << host << "]:" << port;

    {
        flatbuffers::FlatBufferBuilder bufbuilder;
        auto hoststr = bufbuilder.CreateString(host);

        BEGIN_BUILD_MSG_AND_SEND_AT_END(RequestResource, bufbuilder);

        msgbuilder.add_req_id(req_id);
        msgbuilder.add_webkit_resInstNum(resInstNum);
        msgbuilder.add_host(hoststr);
        msgbuilder.add_port(port);
        msgbuilder.add_req_total_size(req_total_size);
        msgbuilder.add_resp_meta_size(resp_meta_size);
        msgbuilder.add_resp_body_size(resp_body_size);
    }

    vlogself(2) << "done";
}


void
IOServiceIPCClient::_on_msg(GenericIpcChannel*, uint8_t type,
                            uint16_t len, const uint8_t *data)
{
    switch (type) {

        IPC_MSG_HANDLER(ReceivedResponse)
        IPC_MSG_HANDLER(DataReceived)
        IPC_MSG_HANDLER(RequestComplete)

    default:
        logself(FATAL) << "invalid IPC message type " << unsigned(type);
        break;
    }
}

void
IOServiceIPCClient::_handle_ReceivedResponse(
    const msgs::ReceivedResponseMsg* msg)
{
    CHECK_NOTNULL(resource_msg_handler_);
    resource_msg_handler_->handle_ReceivedResponse(msg->req_id());
}

void
IOServiceIPCClient::_handle_DataReceived(
    const msgs::DataReceivedMsg* msg)
{
    CHECK_NOTNULL(resource_msg_handler_);
    // vlogself(2) << "req_id= " << msg->req_id()
    //             << " len= " << msg->length();
    resource_msg_handler_->handle_DataReceived(
        msg->req_id(), msg->length());
}

void
IOServiceIPCClient::_handle_RequestComplete(
    const msgs::RequestCompleteMsg* msg)
{
    CHECK_NOTNULL(resource_msg_handler_);
    resource_msg_handler_->handle_RequestComplete(
        msg->req_id(), msg->success());
}

void
IOServiceIPCClient::_on_channel_status(GenericIpcChannel*,
                                       GenericIpcChannel::ChannelStatus status)
{
    DestructorGuard dg(this);

    if (status == GenericIpcChannel::ChannelStatus::CLOSED) {
        ch_status_cb_(this, IOServiceIPCClient::ChannelStatus::CLOSED);
    } else if (status == GenericIpcChannel::ChannelStatus::READY) {
        ch_status_cb_(this, IOServiceIPCClient::ChannelStatus::READY);
    }
}

#undef IPC_MSG_HANDLER
#undef BEGIN_BUILD_MSG_AND_SEND_AT_END
