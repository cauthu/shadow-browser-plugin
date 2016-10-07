#ifndef ipc_hpp
#define ipc_hpp

#include <map>

#include "../utility/stream_server.hpp"
#include "../utility/ipc/generic_ipc_channel.hpp"
#include "../utility/object.hpp"
#include "utility/ipc/transport_proxy/gen/combined_headers"
#include "csp/csp.hpp"

namespace msgs = myipc::transport_proxy::messages;



class IPCServer : public Object
                , public myio::StreamServerObserver
{
public:
    typedef std::unique_ptr<IPCServer, /*folly::*/Destructor> UniquePtr;

    explicit IPCServer(struct event_base*,
                       myio::StreamServer::UniquePtr,
                       csp::ClientSideProxy::UniquePtr);

private:

    virtual ~IPCServer() = default;

    /* StreamServerObserver interface */
    virtual void onAccepted(myio::StreamServer*, StreamChannel::UniquePtr channel) noexcept override;
    virtual void onAcceptError(myio::StreamServer*, int errorcode) noexcept override;

    ////////////

    void _on_msg(uint8_t type, uint16_t len, const uint8_t* data);
    void _on_called(uint32_t id, uint8_t type,
                    uint16_t len, const uint8_t* data);
    void _on_client_channel_status(myipc::GenericIpcChannel::ChannelStatus);

    void _setup_client(myio::StreamChannel::UniquePtr channel);

    void _handle_EstablishTunnel(const uint32_t& id,
                                 const msgs::EstablishTunnelMsg*);
    void _handle_SetAutoStartDefenseOnNextSend(const uint32_t& id,
                                               const msgs::SetAutoStartDefenseOnNextSendMsg*);

    void _handle_StopDefense(const uint32_t& id,
                                 const msgs::StopDefenseMsg*);

    void _on_buflo_channel_ready(csp::ClientSideProxy*);


    struct event_base* evbase_; // don't free
    myio::StreamServer::UniquePtr stream_server_; /* to accept ipc clients */
    /* currently support only one ipc client */
    myipc::GenericIpcChannel::UniquePtr ipc_client_channel_;
    csp::ClientSideProxy::UniquePtr csp_;

    uint32_t establish_tunnel_call_id_;
};

#endif /* end ipc_hpp */
