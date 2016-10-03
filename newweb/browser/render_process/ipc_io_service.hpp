#ifndef ipc_io_service_hpp
#define ipc_io_service_hpp

#include "../../utility/stream_channel.hpp"
#include "../../utility/generic_message_channel.hpp"
#include "../../utility/ipc/generic_ipc_channel.hpp"
#include "../../utility/object.hpp"
// #include "utility/ipc/renderer/gen/combined_headers"
#include "utility/ipc/io_service/gen/combined_headers"

#include "interfaces.hpp"


/* handle io service ipc interface, as a client */

class IOServiceIPCClient : public Object
{
public:
    typedef std::unique_ptr<IOServiceIPCClient, /*folly::*/Destructor> UniquePtr;

    enum class ChannelStatus : short
    {
        READY,
        CLOSED
    };
    typedef boost::function<void(IOServiceIPCClient*, ChannelStatus)> ChannelStatusCb;

    explicit IOServiceIPCClient(struct event_base*,
                                myio::StreamChannel::UniquePtr,
                                ChannelStatusCb);

    
    void set_resource_msg_handler(ResourceMsgHandler* handler)
    {
        resource_msg_handler_ = handler;
    }

    void request_resource(const int& req_id,
                          const uint32_t& resInstNum,
                          const char* host,
                          const uint16_t& port,
                          const size_t& req_total_size,
                          const size_t& resp_meta_size,
                          const size_t& resp_body_size);

    void send_ResetSession();

protected:


    //////

private:

    void _on_msg(myipc::GenericIpcChannel*, uint8_t type,
                 uint16_t len, const uint8_t *data);
    void _on_channel_status(myipc::GenericIpcChannel*,
                            myipc::GenericIpcChannel::ChannelStatus);

    void _handle_ReceivedResponse(const myipc::ioservice::messages::ReceivedResponseMsg* msg);
    void _handle_DataReceived(const myipc::ioservice::messages::DataReceivedMsg* msg);
    void _handle_RequestComplete(const myipc::ioservice::messages::RequestCompleteMsg* msg);


    /////

    ChannelStatusCb ch_status_cb_;
    myipc::GenericIpcChannel::UniquePtr gen_ipc_chan_;

    ResourceMsgHandler* resource_msg_handler_;
};


#endif /* ipc_io_service_hpp */
