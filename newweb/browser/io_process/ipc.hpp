#ifndef ipc_hpp
#define ipc_hpp

#include <map>

#include "../../utility/stream_server.hpp"
#include "../../utility/generic_message_channel.hpp"
#include "../../utility/object.hpp"
#include "utility/ipc/io_service/gen/combined_headers"

namespace msgs = myipc::ioservice::messages;

/* sits on top of and uses a stream server. this should be given to a
 * stream server, and it will take the new stream channels the stream
 * server creates, and create generic message channels out of them,
 * and process the messages that those channels receives
 */
class IPCServer : public Object
                , public myio::StreamServerObserver
                , public myio::GenericMessageChannelObserver
{
public:
    typedef std::unique_ptr<IPCServer, /*folly::*/Destructor> UniquePtr;

    explicit IPCServer(myio::StreamServer::UniquePtr);

private:

    /* StreamServerObserver interface */
    virtual void onAccepted(myio::StreamServer*, StreamChannel::UniquePtr channel) noexcept override;
    virtual void onAcceptError(myio::StreamServer*, int errorcode) noexcept override;

    /* GenericMessageChannelObserver interface */
    virtual void onRecvMsg(myio::GenericMessageChannel*, uint16_t, uint16_t, const uint8_t*) noexcept override;
    virtual void onEOF(myio::GenericMessageChannel*) noexcept override;
    virtual void onError(myio::GenericMessageChannel*, int errorcode) noexcept override;

    void _recv_Hello(const msgs::HelloMsg* msg);
    void _recv_Fetch(const msgs::FetchMsg* msg);

    //////

    myio::StreamServer::UniquePtr stream_server_;

    /* map key is channel objId */
    std::map<uint32_t, myio::GenericMessageChannel::UniquePtr> channels_;
};

#endif /* ipc_hpp */
