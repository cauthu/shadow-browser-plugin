#ifndef ipc_hpp
#define ipc_hpp

#include "../../utility/stream_channel.hpp"
#include "../../utility/generic_message_channel.hpp"


/* sits on top of and uses a stream . also observe the generic message
 * stream channel and handle msgs
 */
class IOServiceIPCClient : public folly::DelayedDestruction
                         , public myio::StreamChannelConnectObserver
                         , public myio::GenericMessageChannelObserver
{
public:
    typedef std::unique_ptr<IOServiceIPCClient, /*folly::*/Destructor> UniquePtr;

    explicit IOServiceIPCClient(myio::StreamChannel::UniquePtr);

protected:

    /******* implement StreamChannelConnectObserver interface *********/
    virtual void onConnected(myio::StreamChannel*) noexcept override;
    virtual void onConnectError(myio::StreamChannel*, int errorcode) noexcept override;
    virtual void onConnectTimeout(myio::StreamChannel*) noexcept override;

    /******* implement GenericMessageChannelObserver interface *********/
    virtual void onRecvMsg(myio::GenericMessageChannel*, uint8_t, uint16_t, const uint8_t*) noexcept override;
    virtual void onEOF(myio::GenericMessageChannel*) noexcept override;
    virtual void onError(myio::GenericMessageChannel*, int errorcode) noexcept override;

    //////

private:

    void _send_Hello();

    myio::StreamChannel::UniquePtr transport_channel_;
    myio::GenericMessageChannel::UniquePtr msg_channel_;
};

#endif /* ipc_hpp */
