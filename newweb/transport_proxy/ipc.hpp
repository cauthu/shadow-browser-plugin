#ifndef ipc_hpp
#define ipc_hpp

#include <map>

#include "../utility/stream_server.hpp"
#include "../utility/generic_message_channel.hpp"
#include "../utility/object.hpp"
#include "utility/ipc/transport_proxy/gen/combined_headers"
#include "csp/csp.hpp"

namespace msgs = myipc::transport_proxy::messages;



class IPCServer : public Object
                , public myio::StreamServerObserver
                , public myio::GenericMessageChannelObserver
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

    /* GenericMessageChannelObserver interface */
    virtual void onRecvMsg(myio::GenericMessageChannel*, uint8_t, uint16_t, const uint8_t*) noexcept override;
    virtual void onEOF(myio::GenericMessageChannel*) noexcept override;
    virtual void onError(myio::GenericMessageChannel*, int errorcode) noexcept override;

    ////////////

    void _setup_client(myio::StreamChannel::UniquePtr channel);
    void _handle_StartDefenseSession(const msgs::StartDefenseSessionMsg* msg);

    void _on_csp_ready(csp::ClientSideProxy*);

    struct event_base* evbase_; // don't free
    myio::StreamServer::UniquePtr stream_server_;
    csp::ClientSideProxy::UniquePtr csp_;

    /* map keys are the routing ids */
    std::map<uint32_t, myio::GenericMessageChannel::UniquePtr> client_channels_;
};

#endif /* end ipc_hpp */
