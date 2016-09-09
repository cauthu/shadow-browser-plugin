
#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"
#include "utility/ipc/io_service/gen/combined_headers"
#include "ipc.hpp"


using myio::StreamChannel;
using myio::GenericMessageChannel;


namespace msgs = myipc::ioservice::messages;
using msgs::type;


IOServiceIPCClient::IOServiceIPCClient(StreamChannel::UniquePtr stream_channel)
    : transport_channel_(std::move(stream_channel))
    , msg_channel_(nullptr)
{
    VLOG(2) << "setting up ipc conn to io process";
    transport_channel_->start_connecting(this);
}

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

void
IOServiceIPCClient::_send_Hello()
{
    flatbuffers::FlatBufferBuilder bufbuilder;
    auto hoststr = bufbuilder.CreateString("cnn.com");

    BEGIN_BUILD_MSG_AND_SEND_AT_END(Fetch, bufbuilder);

    msgbuilder.add_host(hoststr);
    msgbuilder.add_port(80);
    msgbuilder.add_req_total_size(123);
    msgbuilder.add_resp_headers_size(234);
    msgbuilder.add_resp_body_size(29384);
}

void
IOServiceIPCClient::onConnected(StreamChannel* ch) noexcept
{
    CHECK_EQ(transport_channel_.get(), ch);
    VLOG(2) << "transport connected";
    msg_channel_.reset(new GenericMessageChannel(std::move(transport_channel_), this));
    _send_Hello();
}

void
IOServiceIPCClient::onConnectError(StreamChannel* ch, int) noexcept
{
    CHECK_EQ(transport_channel_.get(), ch);
    CHECK(false); // not reached
}

void
IOServiceIPCClient::onConnectTimeout(StreamChannel* ch) noexcept
{
    CHECK_EQ(transport_channel_.get(), ch);
    CHECK(false); // not reached
}

void
IOServiceIPCClient::onRecvMsg(GenericMessageChannel*, uint16_t type,
                              uint16_t len, const uint8_t *data) noexcept
{
    VLOG(2) << "client received msg type " << type;
    switch (type) {
    case type::type_Fetch:
        VLOG(2) << "client received change_priority msg";
        break;
    default:
        CHECK(false); // not reached
        break;
    }
}

void
IOServiceIPCClient::onEOF(GenericMessageChannel*) noexcept
{
    CHECK(false); // not reached
}

void
IOServiceIPCClient::onError(GenericMessageChannel*, int errorcode) noexcept
{
    CHECK(false); // not reached
}

