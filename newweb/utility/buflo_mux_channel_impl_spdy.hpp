#ifndef bulfo_mux_channel_impl_spdy_hpp
#define bulfo_mux_channel_impl_spdy_hpp

#include <memory>

#include <spdylay/spdylay.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <map>
#include <string>
#include <list>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "object.hpp"
#include "buflo_mux_channel.hpp"
#include "timer.hpp"
#include "tcp_channel.hpp"


namespace myio { namespace buflo
{

class BufloMuxChannelImplSpdy : public BufloMuxChannel
                              // , public StreamChannelObserver
                              // , public StreamChannelConnectObserver
{
public:
    // for convenience. DelayedDestruction (see folly's
    // AsyncTransport.h for example)
    typedef std::unique_ptr<BufloMuxChannelImplSpdy, Destructor> UniquePtr;

    BufloMuxChannelImplSpdy(struct event_base*, int fd, bool is_client_side,
                            size_t cell_size,
                            ChannelClosedCb ch_closed_cb,
                            NewStreamConnectRequestCb st_connect_req_cb);

    int create_stream(const char* host,
                      const in_port_t& port,
                      BufloMuxChannelStreamObserver*);

    /* BufloMuxChannel interface */
    virtual int create_stream(const char* host,
                              const in_port_t& port,
                              void* cbdata) override;
    virtual bool set_stream_observer(int sid, BufloMuxChannelStreamObserver*) override;
    virtual bool set_stream_connect_result(int sid, bool ok) override;
    virtual int read(int sid, uint8_t *data, size_t len) override;
    virtual int read_buffer(int sid, struct evbuffer* buf, size_t len) override;
    virtual int drain(int sid, size_t len) override;
    virtual uint8_t* peek(int sid, ssize_t len) override;
    virtual struct evbuffer* get_input_evbuf(int sid) override;
    virtual size_t get_avail_input_length(int sid) const override;
    virtual int write(int sid, const uint8_t *data, size_t len) override;
    virtual int write_buffer(int sid, struct evbuffer *buf) override;
    virtual int write_dummy(int sid, size_t len) override;
    virtual void close_stream(int sid) override;

protected:

    virtual ~BufloMuxChannelImplSpdy();

    void _setup_spdylay_session();
    void _buflo_timer_fired(Timer* timer);
    void _pump_spdy_send();
    void _pump_spdy_recv();

    /* return true if it does append a cell to cell outbuf */
    bool _maybe_add_control_cell_to_outbuf() {return false;}
    /* return true if it does append a cell to cell outbuf */
    bool _maybe_add_data_cell_to_outbuf();

    bool _maybe_flush_data_to_cell_outbuf();

    /* this will add a dummy cell if there is not already a WHOLE
     * dummy cell at the end of cell outbuf. if there's only a partial
     * dummy cell -- which is possible only if part of it has been
     * written to socket -- then we WILL add another one
     *
     * this should be used only when we're actively defending
     */
    void _ensure_a_whole_dummy_cell_at_end_outbuf();
    void _send_cell_outbuf();

    void _consume_input();
    void _handle_non_dummy_input_cell(size_t);
    void _handle_failed_socket_io(const char* io_op_str,
                                  const ssize_t rv,
                                  bool crash_if_EINPROGRESS);
    void _on_socket_eof();
    void _on_socket_error();

    /* shadow doesn't support edge-triggered (epoll) monitoring, so we
     * have to disable write monitoring if we don't have data to
     * write, otherwise will keep getting notified of the write event
     */
    void _maybe_toggle_write_monitoring(bool force_enable=false);

    void      _on_socket_readcb(int fd, short what);
    static void s_socket_readcb(int fd, short what, void* arg);

    void      _on_socket_writecb(int fd, short what);
    static void s_socket_writecb(int fd, short what, void* arg);

    // spdy callbacks
    void      _on_spdylay_before_ctrl_send_cb(spdylay_session *session,
                                              spdylay_frame_type type,
                                              spdylay_frame *frame);
    static void s_spdylay_before_ctrl_send_cb(spdylay_session *session,
                                              spdylay_frame_type type,
                                              spdylay_frame *frame,
                                              void *user_data);

    void      _on_spdylay_on_ctrl_recv_cb(spdylay_session *session,
                                          spdylay_frame_type type,
                                          spdylay_frame *frame);
    static void s_spdylay_on_ctrl_recv_cb(spdylay_session *session,
                                          spdylay_frame_type type,
                                          spdylay_frame *frame,
                                          void *user_data);

    void      _on_spdylay_on_request_recv_cb(const int sid);
    static void s_spdylay_on_request_recv_cb(spdylay_session *session,
                                             int32_t stream_id,
                                             void *user_data);

    ssize_t       _on_spdylay_send_cb(spdylay_session *session,
                                      const uint8_t *data,
                                      size_t length,
                                      int flags);
    static ssize_t s_spdylay_send_cb(spdylay_session *session,
                                     const uint8_t *data,
                                     size_t length,
                                     int flags,
                                     void *user_data);

    ssize_t       _on_spdylay_recv_cb(spdylay_session *session,
                                      uint8_t *data,
                                      size_t length,
                                      int flags);
    static ssize_t s_spdylay_recv_cb(spdylay_session *session,
                                     uint8_t *data,
                                     size_t length,
                                     int flags,
                                     void *user_data);

    class StreamState
    {
    public:
        StreamState()
        {
            inbuf_ = evbuffer_new();
            outbuf_ = evbuffer_new();
        }
        ~StreamState()
        {
            if (inbuf_) {
                evbuffer_free(inbuf_);
                inbuf_ = nullptr;
            }
            if (outbuf_) {
                evbuffer_free(outbuf_);
                outbuf_ = nullptr;
            }
        }

        struct evbuffer* inbuf_;
        struct evbuffer* outbuf_;
    };

    //////////////////////

    struct event_base* evbase_;
    // int fd_; // buflo_mux_channel already has this
    /* using separate events for read and write, so it's easy to
     * enable/disable
     */
    std::unique_ptr<struct event, void(*)(struct event*)> socket_read_ev_;
    std::unique_ptr<struct event, void(*)(struct event*)> socket_write_ev_;

    const size_t cell_size_;
    const size_t cell_body_size_;

    bool defense_active_;
    Timer* buflo_timer_;
    bool whole_dummy_cell_at_end_outbuf_;

    spdylay_session* spdysess_;

    // buffers data for spdy to read and data spdy wants to write
    struct evbuffer* spdy_inbuf_;
    struct evbuffer* spdy_outbuf_;

    // cell in/out bufs are for data that we read from/write into
    // socket
    struct evbuffer* cell_inbuf_;
    struct evbuffer* cell_outbuf_;

    enum CellType : uint8_t
    {
        CONTROL, DATA, DUMMY
    };

    // the state we're in for processing the input
    enum class ReadState
    {
        READ_HEADER = 0, READ_BODY = 1
    };
    struct {
        ReadState state_;
        uint8_t type_;
        uint16_t payload_len_;
        // uint8_t* payload_;

        void reset()
        {
            state_ = ReadState::READ_HEADER;
            payload_len_ = 0;
            // payload_ = nullptr;
        }
    } cell_read_info_;

    std::map<int, std::unique_ptr<StreamState> > stream_states_;


    /*****************  for server-side proxy   *************/

    // // in on_ctrl_recv(), record the "host" header from client, but
    // // only need for a short while: use in on_request_recv()
    // std::map<int, std::string> requested_host_hdr_;

};

}
}

#endif /* bulfo_mux_channel_impl_spdy_hpp */
