
#include "../../utility/easylogging++.h"
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

void
IOServiceIPCClient::_send_Hello()
{
    flatbuffers::FlatBufferBuilder builder;
    msgs::HelloMsgBuilder mb(builder);

    mb.add_resId(319);
    mb.add_xyz(74);
    mb.add_flags(0b111011);

    auto msg = mb.Finish();
    builder.Finish(msg);

    msg_channel_->sendMsg(
        type::type_HELLO, builder.GetSize(), builder.GetBufferPointer());
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
    case type::type_FETCH:
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

