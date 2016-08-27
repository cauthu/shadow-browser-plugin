
#include "io_service_ipc.hpp"

namespace myipc { namespace ioservice {


IPCServer::IPCServer()
{}

void
IPCServer::onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept
{
    // IPCServerProtocol::UniquePtr serverproto;
    // JSONStreamChannel::UniquePtr p(new JSONStreamChannel(std::move(channel), serverproto.get()));
}

void
IPCServer::onAcceptError(StreamServer*, int errorcode) noexcept
{}

} // end namespace ioservice
} // end namespace myipc
