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


typedef boost::function<void(ClientSideProxy*, bool ok)> CSPStatusCb;
typedef boost::function<void(ClientSideProxy*)> ADefenseSessionDoneCb;


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
                             const uint32_t& buflo_packet_intvl_ms,
                             const uint32_t& buflo_L,
                             const uint32_t& buflo_time_limit_secs);

    enum class EstablishReturnValue
    {
        PENDING /* ok, the reconnect is taking place. will call
                 * CSPStatusCb when ready */,
        ALREADY_READY /* the tunnel is currently ready, and reconnect
                       * was not forced. the CSPStatusCb will NOT be
                       * called */,
    };

    EstablishReturnValue establish_tunnel(CSPStatusCb, const bool force_reconnect=true);
    void close_all_streams();

    void set_a_defense_session_done_cb(ADefenseSessionDoneCb cb) { a_defense_session_done_cb_ = cb; }

    bool set_auto_start_defense_session_on_next_send();
    void stop_defense_session(const bool& right_now);

    const uint64_t all_recv_byte_count_so_far() const;
    const uint64_t useful_recv_byte_count_so_far() const;
    const uint32_t dummy_recv_cell_count_so_far() const;

    const uint64_t all_send_byte_count_so_far() const;
    const uint64_t useful_send_byte_count_so_far() const;
    const uint32_t dummy_send_cell_count_so_far() const;

    const uint32_t num_dummy_cells_avoided_so_far() const;

    void start_accepting_clients();

    void log_stats() const;

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

    /* clear the tunnel, the client handlers, pause accepting, etc. */
    void _reset_to_initial();

    void _on_connected_to_ssp();

    void _on_buflo_channel_status(myio::buflo::BufloMuxChannel*,
                                  myio::buflo::BufloMuxChannel::ChannelStatus);

    // the ProxyClientHandler tells us it's closing down
    void _on_client_handler_done(ClientHandler*);

    /* should be called whenever our buflo channel is about to be
     * destroyed, so that we can grab its stats */
    void _update_stats();

    void _log_stats_timer_fired(Timer*);
    void _schedule_log_timer();

    /////////

    struct event_base* evbase_;
    /* server to listen for client connections */
    myio::StreamServer::UniquePtr stream_server_;

    /* these are for pairing with the server-side proxy */
    const std::string peer_host_;
    const in_port_t peer_port_;
    const in_addr_t socks5_addr_;
    const in_port_t socks5_port_;
    uint32_t buflo_cell_size_;
    const uint32_t buflo_packet_intvl_ms_;
    const uint32_t buflo_L_;
    const uint32_t buflo_time_limit_secs_;

    myio::TCPChannel::UniquePtr peer_channel_;
    myio::Socks5Connector::UniquePtr socks_connector_;
    myio::buflo::BufloMuxChannelImplSpdy::UniquePtr buflo_ch_;

    enum class State {
        INITIAL,

        // Connecting to socks5 proxy... the proxy states are optional
        // and skipped if we connecting directly to ssp
        PROXY_CONNECTING,
        PROXY_CONNECTED,
        PROXY_FAILED,

        // Connecting to ssp
        CONNECTING,
            CONNECTED,

            SETTING_UP_BUFLO_CHANNEL,

        READY,
    } state_;

    CSPStatusCb csp_status_cb_;
    ADefenseSessionDoneCb a_defense_session_done_cb_;

    std::map<uint32_t, ClientHandler::UniquePtr> client_handlers_;

    /* we get these from the buflo channel but also take a note of
     * them ourselves when the buflo channel goes away
     */
    uint64_t all_recv_byte_count_so_far_ = 0;
    uint64_t useful_recv_byte_count_so_far_ = 0;
    uint32_t dummy_recv_cell_count_so_far_ = 0;

    uint64_t all_send_byte_count_so_far_ = 0;
    uint64_t useful_send_byte_count_so_far_ = 0;
    uint32_t dummy_send_cell_count_so_far_ = 0;

    uint32_t num_dummy_cells_avoided_so_far_ = 0;

    in_addr_t myaddr_;

    // log stats when current time is a multiple of 30 seconds
    Timer::UniquePtr log_stats_timer_;
};

} // namespace csp

#endif /* CSP_HPP */
