
#include <rapidjson/document.h>

#include "../../utility/easylogging++.h"
#include "../../utility/ipc/io_service_ipc.hpp"
#include "ipc.hpp"


using myio::StreamChannel;
using myio::JSONStreamChannel;


using myipc::ioservice::message_type;


IOServiceIPCClient::IOServiceIPCClient(StreamChannel::UniquePtr stream_channel)
    : transport_channel_(std::move(stream_channel))
    , json_channel_(nullptr)
{
    VLOG(2) << "setting up ipc conn to io process";
    transport_channel_->start_connecting(this);
}

void
IOServiceIPCClient::onConnected(StreamChannel* ch) noexcept
{
    CHECK_EQ(transport_channel_.get(), ch);
    VLOG(2) << "transport connected";
    json_channel_.reset(new myio::JSONStreamChannel(std::move(transport_channel_), this));
    json_channel_->sendMsg(message_type::HELLO);
}

void
IOServiceIPCClient::onConnectError(StreamChannel* ch, int) noexcept
{
    CHECK_EQ(transport_channel_.get(), ch);
    CHECK(false); // not reached
}

void
IOServiceIPCClient::onRecvMsg(JSONStreamChannel*, uint16_t type,
                              const rapidjson::Document&) noexcept
{
    VLOG(2) << "client received msg type " << type;
    switch (type) {
    case message_type::CHANGE_PRIORITY:
        VLOG(2) << "client received change_priority msg";
        break;
    default:
        CHECK(false); // not reached
        break;
    }
}

void
IOServiceIPCClient::onEOF(JSONStreamChannel*) noexcept
{
    CHECK(false); // not reached
}

void
IOServiceIPCClient::onError(JSONStreamChannel*, int errorcode) noexcept
{
    CHECK(false); // not reached
}

