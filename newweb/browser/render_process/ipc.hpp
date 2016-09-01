#ifndef ipc_hpp
#define ipc_hpp

#include "../../utility/stream_channel.hpp"
#include "../../utility/json_stream_channel.hpp"


/* sits on top of and uses a stream . also observe the json
 * stream channel and handle msgs
 */
class IOServiceIPCClient : public folly::DelayedDestruction
                         , public myio::StreamChannelConnectObserver
                         , public myio::JSONStreamChannelObserver
{
public:
    typedef std::unique_ptr<IOServiceIPCClient, /*folly::*/Destructor> UniquePtr;

    explicit IOServiceIPCClient(myio::StreamChannel::UniquePtr);

protected:

    /******* implement StreamChannelConnectObserver interface *********/
    virtual void onConnected(myio::StreamChannel*) noexcept override;
    virtual void onConnectError(myio::StreamChannel*, int errorcode) noexcept override;

    /******* implement JSONStreamChannelObserver interface *********/
    virtual void onRecvMsg(myio::JSONStreamChannel*, uint16_t, const rapidjson::Document&) noexcept override;
    virtual void onEOF(myio::JSONStreamChannel*) noexcept override;
    virtual void onError(myio::JSONStreamChannel*, int errorcode) noexcept override;

    //////

    myio::StreamChannel::UniquePtr transport_channel_;
    myio::JSONStreamChannel::UniquePtr json_channel_;
};

#endif /* ipc_hpp */
