#ifndef io_service_ipc_hpp
#define io_service_ipc_hpp

#include <folly/io/async/DelayedDestruction.h>
#include <memory>

#include "../stream_server.hpp"
#include "../json_stream_channel.hpp"

using myio::StreamServer;
using myio::StreamChannel;

namespace myipc { namespace ioservice {

/*
 * IPC related to IO process/service
 */


/* sits on top of and uses a stream server. when new streams arrive,
 * simply pass them off to the server protocol
 */
class IPCServer : public folly::DelayedDestruction
                , public myio::StreamServerObserver
                , public myio::JSONStreamChannelObserver
{
public:
    typedef std::unique_ptr<IPCServer, /*folly::*/Destructor> UniquePtr;

    explicit IPCServer();

protected:

    /* StreamServerObserver interface */
    virtual void onAccepted(StreamServer*, StreamChannel::UniquePtr channel) noexcept override;
    virtual void onAcceptError(StreamServer*, int errorcode) noexcept override;
};


// class IPCServer : public folly::DelayedDestruction
//                 , public myio::JSONStreamChannelObserver
// {
// public:
//     typedef std::unique_ptr<IPCServerProtocol, /*folly::*/Destructor> UniquePtr;

// };

} // end namespace ioservice
} // end namespace myipc

#endif /* io_service_ipc_hpp */
