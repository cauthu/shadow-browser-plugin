#ifndef ipc_hpp
#define ipc_hpp

#include <map>

#include "../../utility/stream_server.hpp"
#include "../../utility/ipc/generic_ipc_channel.hpp"
#include "../../utility/object.hpp"
#include "utility/ipc/io_service/gen/combined_headers"

#include "net_config.hpp"

namespace msgs = myipc::ioservice::messages;


class HttpNetworkSession;


/* sits on top of and uses a stream server. this should be given to a
 * stream server, and it will take the new stream channels the stream
 * server creates, and create generic message channels out of them,
 * and process the messages that those channels receives
 */
class IPCServer : public Object
                , public myio::StreamServerObserver
{
public:
    typedef std::unique_ptr<IPCServer, /*folly::*/Destructor> UniquePtr;

    explicit IPCServer(struct event_base*,
                       myio::StreamServer::UniquePtr,
                       const NetConfig*);

private:

    virtual ~IPCServer() = default;

    /* StreamServerObserver interface */
    virtual void onAccepted(myio::StreamServer*, StreamChannel::UniquePtr channel) noexcept override;
    virtual void onAcceptError(myio::StreamServer*, int errorcode) noexcept override;

    ///////////

    void _handle_Hello(const uint32_t&, const msgs::HelloMsg* msg);
    void _handle_Fetch(const uint32_t&, const msgs::FetchMsg* msg);

    void _setup_client(StreamChannel::UniquePtr);
    void _remove_route(const uint32_t& routing_id);

    void _on_msg_recv(myipc::GenericIpcChannel*, uint8_t,
                      uint16_t, const uint8_t*);
    void _on_called(myipc::GenericIpcChannel*, uint32_t, uint8_t,
                      uint16_t, const uint8_t*);
    void _on_channel_status(myipc::GenericIpcChannel*,
                            myipc::GenericIpcChannel::ChannelStatus);

    //////

    struct event_base* evbase_; // don't free
    myio::StreamServer::UniquePtr stream_server_;
    const NetConfig* netconf_; // don't free

    /* we will using the objId as the routing IDs
     *
     * therefore the objIds MUST be unique
     */

    /* map keys are the routing ids */
    std::map<uint32_t, myipc::GenericIpcChannel::UniquePtr> client_channels_;

    /* multiple clients, with different routing ids, can share the
     * same session */
    std::map<uint32_t, std::shared_ptr<HttpNetworkSession> > hsessions_;
};

#endif /* end ipc_hpp */
