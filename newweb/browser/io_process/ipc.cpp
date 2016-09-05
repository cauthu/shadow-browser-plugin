
#include "ipc.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "utility/ipc/gen/io_service_messages_flatbuffers_generated.h"

using myio::StreamServer;
using myio::JSONStreamChannel;


using myipc::ioservice::messages::type;

IPCServer::IPCServer(StreamServer::UniquePtr streamserver)
    : stream_server_(std::move(streamserver))
{
    stream_server_->set_observer(this);
    VLOG(2) << "tell stream server to start accepting";
    stream_server_->start_accepting();
}

void
IPCServer::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    const auto id = channel->objId();
    VLOG(2) << "ipc server got new client, objid= " << id;
    JSONStreamChannel::UniquePtr ch(new JSONStreamChannel(std::move(channel), this));
    const auto ret = channels_.insert(make_pair(id, std::move(ch)));
    CHECK(ret.second); // insist it was newly inserted

    myipc::ioservice::messages::HelloMsg hm;
    CHECK(hm.xyz() == 0);
}

void
IPCServer::onAcceptError(StreamServer*, int errorcode) noexcept
{
    LOG(WARNING) << "IPC server has accept error: " << strerror(errorcode);
}

void
IPCServer::onRecvMsg(myio::JSONStreamChannel* channel, uint16_t type,
                     const rapidjson::Document&) noexcept
{
    switch (type) {
    case type::type_HELLO:
        break;
    default:
        CHECK(false) << "invalid IPC message type " << type;
        break;
    }

}

void
IPCServer::onEOF(myio::JSONStreamChannel* ch) noexcept
{
    CHECK(inMap(channels_, ch->objId()));
    VLOG(2) << "ipc server client stream " << ch << " eof";
    channels_[ch->objId()] = nullptr;
}

void
IPCServer::onError(myio::JSONStreamChannel* ch, int errorcode) noexcept
{
    CHECK(inMap(channels_, ch->objId()));
    LOG(WARNING) << "ipc server client stream " << ch << " error: "
                 << strerror(errorcode);
    channels_[ch->objId()] = nullptr;
}
