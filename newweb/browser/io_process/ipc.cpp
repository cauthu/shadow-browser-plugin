
#include "ipc.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "utility/ipc/io_service/gen/combined_headers"


using myio::StreamServer;
using myio::GenericMessageChannel;

namespace msgs = myipc::ioservice::messages;
using msgs::type;

IPCServer::IPCServer(StreamServer::UniquePtr streamserver)
    : stream_server_(std::move(streamserver))
{
    stream_server_->set_observer(this);
    VLOG(2) << "tell stream server to start accepting";
    stream_server_->start_accepting();
}

void
IPCServer::_recv_Hello(const msgs::HelloMsg* msg)
{
    VLOG(2) << "received HelloMsg: " << msg->resId() << ", " << msg->xyz();
}

void
IPCServer::_recv_Fetch(const msgs::FetchMsg* msg)
{
    VLOG(2) << "received FetchMsg: " << msg->host() << ", " << msg->port();
}

void
IPCServer::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    const auto id = channel->objId();
    VLOG(2) << "ipc server got new client, objid= " << id;
    GenericMessageChannel::UniquePtr ch(new GenericMessageChannel(std::move(channel), this));
    const auto ret = channels_.insert(make_pair(id, std::move(ch)));
    CHECK(ret.second); // insist it was newly inserted
}

void
IPCServer::onAcceptError(StreamServer*, int errorcode) noexcept
{
    LOG(WARNING) << "IPC server has accept error: " << strerror(errorcode);
}

void
IPCServer::onRecvMsg(GenericMessageChannel* channel, uint16_t type,
                     uint16_t len, const uint8_t* data) noexcept
{
    VLOG(2) << "recv'ed msg type= " << type << ", len= " << len;
    switch (type) {
    case type::type_HELLO:
        _recv_Hello(msgs::GetHelloMsg(data));
        break;
    case type::type_FETCH:
        _recv_Fetch(msgs::GetFetchMsg(data));
        break;
    default:
        CHECK(false) << "invalid IPC message type " << type;
        break;
    }

}

void
IPCServer::onEOF(GenericMessageChannel* ch) noexcept
{
    CHECK(inMap(channels_, ch->objId()));
    VLOG(2) << "ipc server client stream " << ch << " eof";
    channels_[ch->objId()] = nullptr;
}

void
IPCServer::onError(GenericMessageChannel* ch, int errorcode) noexcept
{
    CHECK(inMap(channels_, ch->objId()));
    LOG(WARNING) << "ipc server client stream " << ch << " error: "
                 << strerror(errorcode);
    channels_[ch->objId()] = nullptr;
}
