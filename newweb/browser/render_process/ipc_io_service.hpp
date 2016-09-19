#ifndef ipc_io_service_hpp
#define ipc_io_service_hpp

#include "../../utility/stream_channel.hpp"
#include "../../utility/generic_message_channel.hpp"
#include "../../utility/ipc/generic_ipc_channel.hpp"
#include "../../utility/object.hpp"
#include "utility/ipc/renderer/gen/combined_headers"


/* handle io service ipc interface, as a client */

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


#endif /* ipc_io_service_hpp */
