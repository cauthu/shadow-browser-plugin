
#include "ipc.hpp"
#include "../../utility/myassert.h"

using myio::StreamServer;
using myio::JSONStreamChannel;


static uint32_t
s_next_client_id(void)
{
    static uint32_t next = 0;
    return ++next;
}

IPCServer::IPCServer()
{}

void
IPCServer::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    JSONStreamChannel::UniquePtr ch(new JSONStreamChannel(std::move(channel), this));
    const auto ret = channels_.insert(make_pair(ch->instNum(), std::move(ch)));
    myassert(ret.second); // insist it was newly inserted
}

void
IPCServer::onAcceptError(StreamServer*, int errorcode) noexcept
{}
