#ifndef ipc_renderer_hpp
#define ipc_renderer_hpp

#include "../../utility/stream_channel.hpp"
#include "../../utility/stream_server.hpp"
#include "../../utility/generic_message_channel.hpp"
#include "../../utility/ipc/generic_ipc_channel.hpp"
#include "../../utility/object.hpp"
#include "utility/ipc/renderer/gen/combined_headers"

#include "interfaces.hpp"


/* handles clients of renderer ipc interface */
class IPCServer : public Object
                , public myio::StreamServerObserver
{
public:
    typedef std::unique_ptr<IPCServer, /*folly::*/Destructor> UniquePtr;

    explicit IPCServer(struct event_base*,
                       myio::StreamServer::UniquePtr);

    void send_PageLoaded(const uint64_t ttfb_ms);

    void set_driver_msg_handler(DriverMsgHandler* handler)
    {
        driver_msg_handler_ = handler;
    }

private:

    /* StreamServerObserver interface */
    virtual void onAccepted(myio::StreamServer*, StreamChannel::UniquePtr channel) noexcept override;
    virtual void onAcceptError(myio::StreamServer*, int errorcode) noexcept override;

    ////////////

    void _on_msg(uint8_t type, uint16_t len, const uint8_t* data);
    void _on_called(uint32_t id, uint8_t type,
                    uint16_t len, const uint8_t* data);
    void _on_client_channel_status(myipc::GenericIpcChannel::ChannelStatus);

    void _setup_client(myio::StreamChannel::UniquePtr channel);

    void _handle_LoadPage(const uint32_t& id,
                      const myipc::renderer::messages::LoadPageMsg*);


    struct event_base* evbase_; // don't free
    myio::StreamServer::UniquePtr stream_server_; /* to accept ipc clients */
    /* currently support only one ipc client */
    myipc::GenericIpcChannel::UniquePtr ipc_client_channel_;

    DriverMsgHandler* driver_msg_handler_;

    uint32_t load_call_id_;
};

#endif /* ipc_renderer_hpp */
