#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <memory>
#include <unistd.h> /* close */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <netdb.h>
#include "myassert.h"
#include <time.h>
#include <event2/event.h>
#include <event2/buffer.h>

#include <boost/function.hpp>

#include "object.hpp"
#include "common.hpp"
#include "request.hpp"
#include "socks5_connector.hpp"
#include "stream_channel.hpp"
#include "tcp_channel.hpp"

#include <spdylay/spdylay.h>

#include <string>
#include <map>
#include <set>
#include <queue>
#include <deque>

class Connection;

typedef boost::function<void(Connection*)> ConnectionErrorCb;
typedef boost::function<void(Connection*)> ConnectionEOFCb;
typedef boost::function<void(Connection*)> ConnectionFirstRecvByteCb;

typedef boost::function<void(Connection*, const Request*)> ConnectionRequestDoneCb;

typedef void (*PushedMetaCb)(int id, const char* url, ssize_t contentlen,
                             const char **nv, Connection* cnx, void* cb_data);
typedef void (*PushedBodyDataCb)(int id, const uint8_t *data, size_t len,
                                 Connection* cnx, void* cb_data);
typedef void (*PushedBodyDoneCb)(int id, Connection* cnx, void* cb_data);

/* this can be used for a connection towards a "server" (e.g.,
 * directly to webserver, or via a socks5 or spdy proxy).
 *
 * it can talk basic http or spdy with the "server".
 *
 * caveat: if http, chunked encoding not supported, i.e., response
 * must provide content length.
 *
 * submit requests onto this connection by calling
 * submit_request(). the request object will be notified of "meta"
 * (status and headers), body_data, and body_done via callbacks.
 */


class Connection : public Object
                 , public myio::Socks5ConnectorObserver
                 , public myio::StreamChannelConnectObserver
                 , public myio::StreamChannelObserver
{
public:

    typedef std::unique_ptr<Connection, folly::DelayedDestruction::Destructor> UniquePtr;

    /*
     * !!!! the ports should be in host byte order, e.g., 80 for web.
     *
     *
     * if want to use socks5 proxy, both "socks5_addr" and
     * "socks5_port" must be non-zero.
     *
     * the constructor will immediately attempt to establish cnx to
     * the server/proxy.
     *
     * "addr/port": of the final web server, "socks5_addr/port": of
     * the socks5 proxy, "ssp_addr/port": of the ssp proxy.
     *
     * if the final server addr is specified, then ssp addr must not,
     * and vice versa. (where "specified" means "non-zero.")
     */
    Connection(struct event_base *evbase,
               const in_addr_t& addr, const in_port_t& port,
               const in_addr_t& socks5_addr, const in_port_t& socks5_port,
               const in_addr_t& ssp_addr, const in_port_t& ssp_port,
               ConnectionErrorCb error_cb, ConnectionEOFCb eof_cb,
               PushedMetaCb pushed_meta_cb, PushedBodyDataCb pushed_body_data_cb,
               PushedBodyDoneCb pushed_body_done_cb,
               void *cb_data /* for error_cb and eof_cb */,
               const bool& use_spdy
        );

    /* !!!! NOTE: this request will not be copied, so the caller must
     * not free this request until it's been completed (either
     * successfully or erroneously)
    */
    int submit_request(Request* req);
    /* return true in connected state and there is no request/IO being
     * active/queued.
     */
    void set_first_recv_byte_cb(ConnectionFirstRecvByteCb cb) {
        cnx_first_recv_byte_cb_ = cb;
    }
    bool is_idle() const;
    size_t get_queue_size() const
    {
        return submitted_req_queue_.size() + active_req_queue_.size();
    }
    const size_t& get_total_num_sent_bytes() const
    {
        return cumulative_num_sent_bytes_;
    }
    const size_t& get_total_num_recv_bytes() const
    {
        return cumulative_num_recv_bytes_;
    }

    std::queue<Request*> get_active_request_queue() const;
    std::deque<Request*> get_pending_request_queue() const;

    void set_request_done_cb(ConnectionRequestDoneCb cb);

    const bool use_spdy_;

private:

    /***** implement Socks5ConnectorObserver interface */
    virtual void onSocksTargetConnectResult(
        myio::Socks5Connector*, myio::Socks5ConnectorObserver::ConnectResult) noexcept override;

    /***** implement StreamChannelConnectObserver interface */
    virtual void onConnected(myio::StreamChannel*) noexcept override;
    virtual void onConnectError(myio::StreamChannel*, int) noexcept override;
    virtual void onConnectTimeout(myio::StreamChannel*) noexcept override;

    /***** implement StreamChannel interface */
    virtual void onNewReadDataAvailable(myio::StreamChannel*) noexcept override;
    virtual void onWrittenData(myio::StreamChannel*) noexcept override;
    virtual void onEOF(myio::StreamChannel*) noexcept override;
    virtual void onError(myio::StreamChannel*, int errorcode) noexcept override;


    static ssize_t s_spdylay_send_cb(spdylay_session *, const uint8_t *,
                                     size_t, int, void*);
    ssize_t spdylay_send_cb(spdylay_session *session, const uint8_t *data,
                            size_t length, int flags);

    static ssize_t s_spdylay_recv_cb(spdylay_session *, uint8_t *,
                                     size_t, int, void*);
    ssize_t spdylay_recv_cb(spdylay_session *session, uint8_t *buf,
                            size_t length, int flags);

    static void s_spdylay_on_ctrl_recv_cb(spdylay_session *,
                                          spdylay_frame_type,
                                          spdylay_frame *,
                                          void *);
    void spdylay_on_ctrl_recv_cb(spdylay_session *session,
                                 spdylay_frame_type type,
                                 spdylay_frame *frame);

    static void s_spdylay_before_ctrl_send_cb(spdylay_session *,
                                              spdylay_frame_type,
                                              spdylay_frame *,
                                              void *);
    void spdylay_before_ctrl_send_cb(spdylay_session *session,
                                     spdylay_frame_type type,
                                     spdylay_frame *frame);

    /* a spdy DATA frame might be notified via multiple
     * spdylay_on_data_chunk_recv_cb() calls, then finally a
     * spdylay_on_data_recv_cb() call. to know when a response has
     * been completely received, should look at the flags inside the
     * spdylay_on_data_recv_cb()
     */
    static void s_spdylay_on_data_chunk_recv_cb(spdylay_session *,
                                                uint8_t, int32_t,
                                                const uint8_t *, size_t, void *);
    void spdylay_on_data_chunk_recv_cb(spdylay_session *session,
                                       uint8_t flags, int32_t stream_id,
                                       const uint8_t *data, size_t len);

    static void s_spdylay_on_data_recv_cb(spdylay_session *,
                                          uint8_t, int32_t,
                                          int32_t, void *);
    void spdylay_on_data_recv_cb(spdylay_session *session,
                                 uint8_t flags, int32_t stream_id,
                                 int32_t len);

    virtual ~Connection();

    int initiate_connection();

    void _setup_spdylay_session();

    /* this only takes data from whatever output buffers appropriate
     * (spdy or http) and write to socket. this is not responsible for
     * putting data into those buffers.
     */
    void _maybe_send();

    void disconnect();

    // return true if did write to transport, otherwise return false
    void _maybe_http_write_to_transport();

    // read from socket and process the read data
    void _maybe_http_consume_input();

    void handle_server_push_ctrl_recv(spdylay_frame *frame);

    // void _on_eof();
    // void _on_error();

    struct event_base *evbase_; // dont free
    myio::TCPChannel::UniquePtr transport_;
    myio::Socks5Connector::UniquePtr socks_connector_;

    enum class State {
        // Disconnected
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

    const in_addr_t addr_;
    const in_port_t port_;

    const in_addr_t socks5_addr_;
    const in_port_t socks5_port_;
    const in_addr_t ssp_addr_;
    const in_port_t ssp_port_;

    ConnectionErrorCb cnx_error_cb_;
    ConnectionEOFCb cnx_eof_cb_;
    ConnectionFirstRecvByteCb cnx_first_recv_byte_cb_;
    void *cb_data_;
    PushedMetaCb notify_pushed_meta_;
    PushedBodyDataCb notify_pushed_body_data_;
    PushedBodyDoneCb notify_pushed_body_done_;

    ConnectionRequestDoneCb notify_request_done_cb_;

    /* for spdy-to-server support */
    std::unique_ptr<spdylay_session, void(*)(spdylay_session*)> spdysess_;
    /* dont free these Request's. these are only shallow pointer */
    std::map<int32_t, Request*> sid2req_;
    /* set of pushed stream ids */
    std::set<int32_t> psids_;
    /* for http-to-server support */

    /* requests submitted by browser are enqueued in
     * submitted_req_queue_. when a request is written into the
     * outbuf_, it is moved to active_req_queue_.
     */
    std::deque<Request* > submitted_req_queue_; // dont free these ptrs
    std::queue<Request* > active_req_queue_; // dont free these ptrs

    enum class HTTPRespState {
        HTTP_RSP_STATE_STATUS_LINE, /* waiting for a full status line */
        HTTP_RSP_STATE_HEADERS,
        HTTP_RSP_STATE_BODY,
    } http_rsp_state_;
    int http_rsp_status_;
    std::vector<char *> rsp_hdrs_; // DO free every _other_ one of
                                   // these ptrs (i.e., index 0, 2, 4,
                                   // etc)
    ssize_t body_len_; // -1, or amount of data _left_ to read from
                       // server/deliver to user. this is of the
                       // response body only, and not of the full
                       // entity.

    /* total num bytes sent/received on this cnx (not counting the
     * socks handshake, which is negligible) */
    size_t cumulative_num_sent_bytes_;
    size_t cumulative_num_recv_bytes_;

};

#endif /* CONNECTION_HPP */
