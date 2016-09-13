#ifndef CSP_HPP
#define CSP_HPP

#include "../../utility/object.hpp"
#include "../../utility/stream_server.hpp"
#include "../../utility/tcp_channel.hpp"
#include "../../utility/socks5_connector.hpp"
#include "../../utility/buflo_mux_channel.hpp"

#include "../../utility/buflo_mux_channel_impl_spdy.hpp"

#include "client_handler.hpp"

class ClientHandler;

class ClientSideProxy : public Object
                 , public myio::StreamServerObserver
                 , public myio::StreamChannelConnectObserver
                 , public myio::Socks5ConnectorObserver
{
public:
    typedef std::unique_ptr<ClientSideProxy, /*folly::*/Destructor> UniquePtr;

    explicit ClientSideProxy(struct event_base* evbase,
                            myio::StreamServer::UniquePtr,
                            const in_addr_t& peer_addr,
                            const in_port_t& peer_port,
                            const in_addr_t& socks5_addr,
                            const in_port_t& socks5_port);


protected:

    virtual ~ClientSideProxy();

    /* StreamServerObserver interface */
    virtual void onAccepted(myio::StreamServer*,
                            myio::StreamChannel::UniquePtr) noexcept override;
    virtual void onAcceptError(myio::StreamServer*, int) noexcept override;

    /***** implement StreamChannelConnectObserver interface */
    virtual void onConnected(myio::StreamChannel*) noexcept override;
    virtual void onConnectError(myio::StreamChannel*, int) noexcept override;
    virtual void onConnectTimeout(myio::StreamChannel*) noexcept override;

    /***** implement Socks5ConnectorObserver interface */
    virtual void onSocksTargetConnectResult(
        myio::Socks5Connector*, myio::Socks5ConnectorObserver::ConnectResult) noexcept override;


    //////////////

    void _on_connected_to_ssp();

    void _on_buflo_channel_closed(myio::buflo::BufloMuxChannel*);
    // void _on_buflo_channel_stream_id_assigned_cb(
    //     myio::buflo::BufloMuxChannel*,
    //     int, void*);
    // void _on_buflo_channel_stream_create_result_cb(
    //     myio::buflo::BufloMuxChannel*,
    //     int, bool);
    // void _on_buflo_channel_stream_data_cb(myio::buflo::BufloMuxChannel*,
    //                                       int);
    // void _on_buflo_channel_stream_closed_cb(myio::buflo::BufloMuxChannel*,
    //                                         int);

    // the ProxyClientHandler tells us it's closing down
    void _on_client_handler_done(ClientHandler*);

    struct event_base* evbase_;
    /* server to listen for client connections */
    myio::StreamServer::UniquePtr stream_server_;

    /* these are for pairing with the server-side proxy */
    const in_addr_t peer_addr_;
    const in_port_t peer_port_;
    const in_addr_t socks5_addr_;
    const in_port_t socks5_port_;
    myio::TCPChannel::UniquePtr peer_channel_;
    int peer_fd_;
    myio::Socks5Connector::UniquePtr socks_connector_;

    myio::buflo::BufloMuxChannelImplSpdy::UniquePtr buflo_ch_;
    enum class State {
        DISCONNECTED,
        // Connecting to socks5 proxy
        PROXY_CONNECTING,
        PROXY_CONNECTED,
        PROXY_FAILED,
        // Connecting to target (either ssp or destination webserver)
        // -- possibly through socks proxy
        CONNECTING,
        CONNECTED,
        NO_LONGER_USABLE,
        // was connected and now destroyed, so don't use
        DESTROYED,
    } state_;


    std::map<uint32_t, ClientHandler::UniquePtr> client_handlers_;
};


#endif /* CSP_HPP */
