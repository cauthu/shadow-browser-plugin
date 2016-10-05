#ifndef CSP_HPP
#define CSP_HPP

#include "../../utility/object.hpp"
#include "../../utility/stream_server.hpp"
#include "../../utility/tcp_channel.hpp"
#include "../../utility/socks5_connector.hpp"
#include "../../utility/buflo_mux_channel.hpp"

#include "../../utility/buflo_mux_channel_impl_spdy.hpp"

#include "client_handler.hpp"

namespace csp
{

class ClientHandler;
class ClientSideProxy;


typedef boost::function<void(ClientSideProxy*)> CSPReadyCb;


class ClientSideProxy : public Object
                 , public myio::StreamServerObserver
                 , public myio::StreamChannelConnectObserver
                 , public myio::Socks5ConnectorObserver
{
public:
    typedef std::unique_ptr<ClientSideProxy, /*folly::*/Destructor> UniquePtr;

    /* stream server is the server that will listen for/accept proxy
     * clients for data transfer (i.e., socks5 clients)
     *
     * constructor doesn't do anything interesting
     *
     * use "kickstart()" to make the csp start connecting to/setting
     * up its buflo channel with ssp, and start accepting socks5
     * connections
     *
     *
     * "peer": is the server-side proxy (duh! we're the client-side)
     *
     * "socks5_addr": if not zero, then it's the ip address of the
     * socks5 proxy (e.g., local Tor client) that we should use to
     * reach the peer.
     */
    explicit ClientSideProxy(struct event_base* evbase,
                            myio::StreamServer::UniquePtr,
                             const char* peer_host,
                            const in_port_t& peer_port,
                            const in_addr_t& socks5_addr,
                             const in_port_t& socks5_port,
                             const uint32_t& buflo_frequencyMs,
                             const uint32_t& buflo_L);

    enum class EstablishReturnValue
    {
        PENDING /* ok, the reconnect is taking place. will call
                 * CSPReadyCb when ready */,
        ALREADY_READY /* the tunnel is currently ready, and reconnect
                       * was not forced. the CSPReadyCb will NOT be
                       * called */,
    };

    EstablishReturnValue establish_tunnel(CSPReadyCb, const bool force_reconnect=true);

    void set_auto_start_defense_session_on_next_send();
    void stop_defense_session(const bool& right_now);

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

    void _on_buflo_channel_status(myio::buflo::BufloMuxChannel*,
                                  myio::buflo::BufloMuxChannel::ChannelStatus);

    // the ProxyClientHandler tells us it's closing down
    void _on_client_handler_done(ClientHandler*);

    struct event_base* evbase_;
    /* server to listen for client connections */
    myio::StreamServer::UniquePtr stream_server_;

    /* these are for pairing with the server-side proxy */
    const std::string peer_host_;
    const in_port_t peer_port_;
    const in_addr_t socks5_addr_;
    const in_port_t socks5_port_;
    const uint32_t buflo_frequencyMs_;
    const uint32_t buflo_L_;

    myio::TCPChannel::UniquePtr peer_channel_;
    myio::Socks5Connector::UniquePtr socks_connector_;
    myio::buflo::BufloMuxChannelImplSpdy::UniquePtr buflo_ch_;

    enum class State {
        INITIAL,
        // Connecting to socks5 proxy
        PROXY_CONNECTING,
        PROXY_CONNECTED,
        PROXY_FAILED,
        // Connecting to target (either ssp or destination webserver)
        // -- possibly through socks proxy
        CONNECTING,
        READY,
    } state_;

    CSPReadyCb ready_cb_;

    std::map<uint32_t, ClientHandler::UniquePtr> client_handlers_;
};

} // namespace csp

#endif /* CSP_HPP */
