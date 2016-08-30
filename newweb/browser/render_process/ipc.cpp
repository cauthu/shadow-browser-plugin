
#include <rapidjson/document.h>

#include "../../utility/myassert.h"
#include "../../utility/logging.hpp"
#include "../../utility/ipc/io_service_ipc.hpp"
#include "ipc.hpp"


using myio::StreamChannel;
using myio::JSONStreamChannel;


static uint32_t
s_get_instNum(void)
{
    static uint32_t next = 0;
    return ++next;
}


using myipc::ioservice::message_type;


IOServiceIPCClient::IOServiceIPCClient(StreamChannel::UniquePtr stream_channel)
    : transport_channel_(std::move(stream_channel))
    , json_channel_(nullptr)
    , instNum_(s_get_instNum())
{
    transport_channel_->start_connecting(this);
}

void
IOServiceIPCClient::onConnected(StreamChannel* ch) noexcept
{
    myassert(transport_channel_.get() == ch);
    MYLOG(INFO) << ("transport connected");
    json_channel_.reset(new myio::JSONStreamChannel(std::move(transport_channel_), this));
    json_channel_->sendMsg(message_type::HELLO);
}

void
IOServiceIPCClient::onConnectError(StreamChannel* ch, int) noexcept
{
    myassert(transport_channel_.get() == ch);
    myassert(false);
}

void
IOServiceIPCClient::onRecvMsg(JSONStreamChannel*, uint16_t type,
                              const rapidjson::Document&) noexcept
{
    MYLOG(INFO) << "client received msg type " << type;
    switch (type) {
    case message_type::CHANGE_PRIORITY:
        MYLOG(INFO) << "client received change_priority msg";
        break;
    default:
        myassert(false);
        break;
    }
}

void
IOServiceIPCClient::onEOF(JSONStreamChannel*) noexcept
{
    myassert(false);
}

void
IOServiceIPCClient::onError(JSONStreamChannel*, int errorcode) noexcept
{
    myassert(false);
}

