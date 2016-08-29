#ifndef ipc_hpp
#define ipc_hpp

#include <map>

#include "../../utility/stream_server.hpp"
#include "../../utility/json_stream_channel.hpp"


/* sits on top of and uses a stream server. this should be given to a
stream server, and it will take the new stream channels the stream
server creates, and create json stream channels out of them, and
process the json messages that those channels receives
 */
class IPCServer : public folly::DelayedDestruction
                , public myio::StreamServerObserver
                , public myio::JSONStreamChannelObserver
{
public:
    typedef std::unique_ptr<IPCServer, /*folly::*/Destructor> UniquePtr;

    explicit IPCServer(myio::StreamServer::UniquePtr);

protected:

    /* StreamServerObserver interface */
    virtual void onAccepted(myio::StreamServer*, StreamChannel::UniquePtr channel) noexcept override;
    virtual void onAcceptError(myio::StreamServer*, int errorcode) noexcept override;

    /* JSONStreamChannelObserver interface */
    virtual void onRecvMsg(myio::JSONStreamChannel*, uint16_t, const rapidjson::Document&) noexcept override;
    virtual void onEOF(myio::JSONStreamChannel*) noexcept override;
    virtual void onError(myio::JSONStreamChannel*, int errorcode) noexcept override;

    //////

    myio::StreamServer::UniquePtr stream_server_;
    std::map<uint32_t, myio::JSONStreamChannel::UniquePtr> channels_;
};

#endif /* ipc_hpp */
