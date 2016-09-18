#ifndef ipc_hpp
#define ipc_hpp

#include "../../utility/stream_channel.hpp"
#include "../../utility/generic_message_channel.hpp"
#include "../../utility/ipc/generic_ipc_channel.hpp"
#include "../../utility/object.hpp"


class IOServiceIPCClient : public Object
{
public:
    typedef std::unique_ptr<IOServiceIPCClient, /*folly::*/Destructor> UniquePtr;

    explicit IOServiceIPCClient(struct event_base*,
                                myio::StreamChannel::UniquePtr);

protected:


    //////

private:

    void _send_Hello();

    void _on_msg(myipc::GenericIpcChannel*, uint8_t type,
                 uint16_t len, const uint8_t *data);
    void _on_channel_status(myipc::GenericIpcChannel*,
                            myipc::GenericIpcChannel::ChannelStatus);

    myipc::GenericIpcChannel::UniquePtr gen_ipc_client_;
};

#endif /* ipc_hpp */
