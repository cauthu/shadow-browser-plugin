
#include "ipc.hpp"
#include "../../utility/myassert.h"
#include "../../utility/logging.hpp"
#include "../../utility/ipc/io_service_ipc.hpp"

using myio::StreamServer;
using myio::JSONStreamChannel;


static uint32_t
s_next_client_id(void)
{
    static uint32_t next = 0;
    return ++next;
}

using myipc::ioservice::message_type;

IPCServer::IPCServer(StreamServer::UniquePtr streamserver)
    : stream_server_(std::move(streamserver))
{
    stream_server_->set_observer(this);
    MYLOG(INFO) << ("tell stream server to start accepting");
    stream_server_->start_accepting();
}

void
IPCServer::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    MYLOG(INFO) << "ipc server got new client stream " << channel.get() << " ppp";
    JSONStreamChannel::UniquePtr ch(new JSONStreamChannel(std::move(channel), this));
    // ch->sendMsg(message_type::FETCH);
    const auto ret = channels_.insert(make_pair(ch->instNum(), std::move(ch)));
    myassert(ret.second); // insist it was newly inserted
}

void
IPCServer::onAcceptError(StreamServer*, int errorcode) noexcept
{}

void
IPCServer::onRecvMsg(myio::JSONStreamChannel* channel, uint16_t type,
                     const rapidjson::Document&) noexcept
{
    switch (type) {
    case message_type::HELLO:
        MYLOG(INFO) << ("    servre got hello msg");
        channel->sendMsg(message_type::CHANGE_PRIORITY);
        break;
    default:
        myassert(false);
        break;
    }

}

void
IPCServer::onEOF(myio::JSONStreamChannel* ch) noexcept
{
    MYLOG(WARNING) << "    ipc server client stream " << ch << " eof";
}

void
IPCServer::onError(myio::JSONStreamChannel* ch, int errorcode) noexcept
{
    MYLOG(WARNING) << "    ipc server client stream " << ch << " ERROR";
}

