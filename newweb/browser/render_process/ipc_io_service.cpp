
#include <boost/bind.hpp>

#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"
#include "utility/ipc/io_service/gen/combined_headers"
#include "utility/ipc/transport_proxy/gen/combined_headers"
#include "ipc_io_service.hpp"


using myio::StreamChannel;
using myipc::GenericIpcChannel;


namespace msgs = myipc::ioservice::messages;
using msgs::type;


IOServiceIPCClient::IOServiceIPCClient(struct event_base* evbase,
                                       StreamChannel::UniquePtr stream_channel)
{
    VLOG(2) << "setting up ipc conn to io process";
    gen_ipc_client_.reset(
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
        msg_channel_->sendMsg(                                          \
            __type, bufbuilder.GetSize(),                               \
            bufbuilder.GetBufferPointer());                             \
    }

#define IPC_MSG_HANDLER(TYPE)                                           \
    case msgs::type_ ## TYPE: {                                         \
        _handle_ ## TYPE(msgs::Get ## TYPE ## Msg(data));               \
    }                                                                   \
    break;

void
IOServiceIPCClient::_on_msg(GenericIpcChannel*, uint8_t type,
                            uint16_t len, const uint8_t *data)
{
    // switch (type) {

    //     IPC_MSG_HANDLER(Load)

    // default:
    //     logself(FATAL) << "invalid IPC message type " << unsigned(type);
    //     break;
    // }
}

void
IOServiceIPCClient::_on_channel_status(GenericIpcChannel*,
                                       GenericIpcChannel::ChannelStatus status)
{
    if (status == GenericIpcChannel::ChannelStatus::CLOSED) {
        LOG(FATAL) << "trouble";
    }
}

#undef IPC_MSG_HANDLER
#undef BEGIN_BUILD_MSG_AND_SEND_AT_END
