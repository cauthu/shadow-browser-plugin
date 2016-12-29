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
#include <deque>

#include "object.hpp"
#include "buflo_mux_channel.hpp"
#include "timer.hpp"
#include "tcp_channel.hpp"


namespace myio { namespace buflo
{

typedef boost::function<void(BufloMuxChannel*)> ChannelReadyCb;

class BufloMuxChannelImplSpdy : public BufloMuxChannel
{
public:
    // for convenience. DelayedDestruction (see folly's
    // AsyncTransport.h for example)
    typedef std::unique_ptr<BufloMuxChannelImplSpdy, Destructor> UniquePtr;

    /* "cell_size" can be two values:
     *
     * * 0 means we are not sending cells at all, i.e., no buflo
     * stuff. just a straight spdy proxy. NOTE that this applies only
     * to our sending; the other peer can still send cells, and we
     * still receive them correctly.
     *
     * * 750 means we will be sending cells of 750 bytes, with padding
     * if necessary. again, the peer can independently choose to use 0
     * or 750
     *
     * "myaddr" will be sent to the other end, to help
     * troublingshooting. should be in host-byte order
     *
     * "defense_session_time_limit": the maximum amount of time in
     * seconds a defense session is allowed to be active.  if it's
     * reached, a client side channel will crash. a server side
     * channel will automatically stop its side of the defense, of
     * course, the client side might continue its side of the defense
     *
     * if 0 is speficied, will use default_defense_session_time_limit
     */

#ifdef IN_SHADOW
    static const uint32_t default_defense_session_time_limit = 60;
#else
    static const uint32_t default_defense_session_time_limit = 30;
#endif

    BufloMuxChannelImplSpdy(struct event_base*, int fd, bool is_client_side,
                            const in_addr_t& myaddr,
                            size_t cell_size,
                            const uint32_t& tamaraw_pkt_intvl_ms,
                            const uint32_t& tamaraw_L,
                            const uint32_t& defense_session_time_limit,
                            ChannelStatusCb ch_status_cb,
                            NewStreamConnectRequestCb st_connect_req_cb);

    virtual int create_stream2(const char* host,
                               const in_port_t& port,
                               BufloMuxChannelStreamObserver*) override;

    /* this starts the timer but will NOT do any immediate write,
     * i.e., it will start writing at the timer fired
     */
    virtual bool start_defense_session() override;
    virtual void stop_defense_session(bool right_now=false) override;
    virtual void set_auto_start_defense_session_on_next_send() override;

    /* BufloMuxChannel interface */
    virtual int create_stream(const char* host,
                              const in_port_t& port,
                              void* cbdata) override;
    virtual bool set_stream_observer(int sid, BufloMuxChannelStreamObserver*) override;
    virtual bool set_stream_connected(int sid) override;
    // virtual int read(int sid, uint8_t *data, size_t len) override;
    virtual int read_buffer(int sid, struct evbuffer* buf, size_t len) override;
    virtual int drain(int sid, size_t len) override;
    virtual uint8_t* peek(int sid, ssize_t len) override;
    virtual struct evbuffer* get_input_evbuf(int sid) override;
    virtual size_t get_avail_input_length(int sid) const override;
    // virtual int write(int sid, const uint8_t *data, size_t len) override;
    virtual int write_buffer(int sid, struct evbuffer *buf) override;
    virtual int set_write_eof(int sid) override;

    // virtual int write_dummy(int sid, size_t len) override;
    virtual void close_stream(int sid) override;

    std::string peer_ip() const;

    const uint64_t& all_recv_byte_count() const { return all_recv_byte_count_; }
    const uint64_t& useful_recv_byte_count() const { return all_users_data_recv_byte_count_; }
    const uint32_t& dummy_recv_cell_count() const { return dummy_recv_cell_count_; }

    const uint64_t& all_send_byte_count() const { return all_send_byte_count_; }
    const uint64_t& useful_send_byte_count() const { return all_users_data_send_byte_count_; }
    const uint32_t& dummy_send_cell_count() const { return dummy_send_cell_count_; }

    const uint32_t& num_dummy_cells_avoided() const { return num_dummy_cells_avoided_; }

protected:

    virtual ~BufloMuxChannelImplSpdy();

    void _setup_spdylay_session();
    void _buflo_timer_fired(Timer* timer);
    void _pump_spdy_send(const bool log_flushed_cell_count=false);
    void _pump_spdy_recv();

    void _fill_my_peer_info_outbuf();
    void _write_my_peer_info_outbuf();
    void _read_peer_info();
    void _close_socket_and_events();

    /* return true if it did add a cell to cell outbuf */
    bool _maybe_add_control_cell_to_outbuf() { CHECK(0) << "todo"; return false; }
    /* return true if it did add a cell to cell outbuf */
    bool _maybe_add_ONE_data_cell_to_outbuf();
    void _add_ONE_dummy_cell_to_outbuf();
    bool _maybe_drop_whole_dummy_cell_at_end_outbuf(const int from_line,
                                                    const bool=true);

    bool _maybe_set_cell_flags(uint8_t* type_n_flags,
                               const char* cell_type);

    /* WILL move all data into cell outbuf. the current defense state
     * must be NONE */
    size_t _maybe_flush_data_to_cell_outbuf();

    /* this will add a dummy cell if there is not already a WHOLE
     * dummy cell at the end of cell outbuf. if there's only a partial
     * dummy cell -- which is possible only if part of it has been
     * written to socket -- then we WILL add another one
     *
     * this should be used only when we're actively defending
     */
    void _ensure_a_whole_dummy_cell_at_end_outbuf();
    void _send_cell_outbuf();

    void _read_cells();
    void _handle_input_cell();
    void _handle_failed_socket_io(const char* io_op_str,
                                  const ssize_t rv,
                                  bool crash_if_EINPROGRESS);
    void _on_socket_eof();
    void _on_socket_error();
    void _notify_a_defense_session_done();
    void _init_stream_state(const int&);
    void _init_stream_data_provider(const int& sid);

    void _update_output_cell_progress(int num_written);

    /* shadow doesn't support edge-triggered (epoll) monitoring, so we
     * have to disable write monitoring if we don't have data to
     * write, otherwise will keep getting notified of the write event
     */
    enum class ForceToggleMode
    {
        NONE,
        FORCE_ENABLE /* enable regardless of outbuf */,
        FORCE_DISABLE /* disable regardless of outbuf */
    };
    void _maybe_toggle_write_monitoring(ForceToggleMode);

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

    ssize_t      _on_spdylay_data_read_cb(spdylay_session *session,
                                          int32_t stream_id,
                                          uint8_t *buf, size_t length,
                                          int *eof,
                                          spdylay_data_source *source);
    static ssize_t s_spdylay_data_read_cb(spdylay_session *session,
                                          int32_t stream_id,
                                          uint8_t *buf, size_t length,
                                          int *eof,
                                          spdylay_data_source *source,
                                          void *user_data);

    void      _on_spdylay_on_stream_close_cb(spdylay_session *session,
                                             int32_t stream_id,
                                             spdylay_status_code status_code);
    static void s_spdylay_on_stream_close_cb(spdylay_session *session,
                                             int32_t stream_id,
                                             spdylay_status_code status_code,
                                             void *user_data);

    void      _on_spdylay_on_data_chunk_recv_cb(spdylay_session *session,
                                                uint8_t flags,
                                                int32_t stream_id,
                                                const uint8_t *data,
                                                size_t len);
    static void s_spdylay_on_data_chunk_recv_cb(spdylay_session *session,
                                                uint8_t flags,
                                                int32_t stream_id,
                                                const uint8_t *data,
                                                size_t len,
                                                void *user_data);

    void         _on_spdylay_on_data_recv_cb(spdylay_session *session,
                                             uint8_t flags,
                                             int32_t stream_id,
                                             int32_t length);
    static void s_spdylay_on_data_recv_cb(spdylay_session *session,
                                             uint8_t flags,
                                             int32_t stream_id,
                                             int32_t length,
                                             void *user_data);

    bool _check_L(const uint16_t& L) const;

    class StreamState
    {
    public:
        StreamState()
        {
            inward_buf_ = evbuffer_new();
            outward_buf_ = evbuffer_new();
            inward_deferred_ = false;
            inward_has_seen_eof_ = false;

            total_recv_from_inner_ = 0;
            inner_recv_eof_ = false;
        }
        ~StreamState()
        {
            if (inward_buf_) {
                evbuffer_free(inward_buf_);
                inward_buf_ = nullptr;
            }
            if (outward_buf_) {
                evbuffer_free(outward_buf_);
                outward_buf_ = nullptr;
            }
        }

        // stores data from outer-side of the tunnel (i.e., the client
        // or the server) towards the tunnel stream. spdy will read
        // from this buf
        struct evbuffer* inward_buf_;
        bool inward_deferred_; /* when there's currently no data for
                                * spdy to read, we will tell it to
                                * stop trying to read from this
                                * stream. so when have data again,
                                * resume it
                                */
        bool inward_has_seen_eof_; /* set to true when the outer
                                    * stream has closed gracefully.
                                    * we will continue to try to send
                                    * any buffered data into the
                                    * tunnel
                                    */

        /* num bytes received from inner (to be given to outer) */
        uint32_t total_recv_from_inner_;
        /* we have received the last data frame from inner stream */
        bool inner_recv_eof_;

        // stores data spdy receives from the tunnel stream to be sent
        // outward, i.e., to client or server
        struct evbuffer* outward_buf_;
    };

    //////////////////////

    struct event_base* evbase_;
    // int fd_; // buflo_mux_channel already has this
    /* using separate events for read and write, so it's easy to
     * enable/disable
     */
    std::unique_ptr<struct event, void(*)(struct event*)> socket_read_ev_;
    std::unique_ptr<struct event, void(*)(struct event*)> socket_write_ev_;

    /* for cells that we send */
    const size_t cell_size_;
    const size_t cell_body_size_;
    const uint32_t tamaraw_pkt_intvl_ms_;
    uint32_t tamaraw_L_;

    /* for cells that the peer sends and we receive */
    size_t peer_cell_size_;
    size_t peer_cell_body_size_;

    /* in host byte order */
    const in_addr_t myaddr_;
    in_addr_t peeraddr_;

    const uint32_t defense_session_time_limit_;

    enum class DefenseState
    {
        NONE = 0,
            PENDING_NEXT_SOCKET_SEND /* want to start defense,
                                        * but not until the next
                                        * time we can write to
                                        * socket. i.e., when
                                        * PENDING_NEXT_SOCKET_SEND,
                                        * the timer is NOT
                                        * running */,
            ACTIVE /* the buflo timer is running */,
    };
    struct {
        /* note on interaction with
         * auto_start_defense_session_on_next_send_: after
         * auto_start_defense_session_on_next_send_ is set, the
         * defense is NOT actually started until the first time we are
         * able to write to socket */
        void reset()
        {
            state = DefenseState::NONE;
            num_data_cells_added = 0;
            num_write_attempts = 0;
            stop_requested = false;

            need_start_flag_in_next_cell = false;
            need_stop_flag_in_next_cell = false;
            need_auto_stopped_flag_in_next_cell = false;

            evutil_timerclear(&auto_stop_time_point);
        }

        bool is_done_defending_send(const uint8_t& L) const
        {
            CHECK_EQ(state, DefenseState::ACTIVE);
            return (stop_requested && (0 == (num_write_attempts % L)));
        }

        void increment_send_attempt()
        {
            CHECK_EQ(state, DefenseState::ACTIVE);
            ++num_write_attempts;
        }

        void request_stop()
        {
            CHECK_EQ(state, DefenseState::ACTIVE);
            CHECK(!stop_requested);
            stop_requested = true;
            need_start_flag_in_next_cell = false;
        }

        DefenseState state;

        /* "num_data_cells_added" is number of *added* to
         * cell_outbuf_, during either pending or active.
         *
         * !!!! NOTE !!!!  that this is NOT the number of cells we
         * have attempted to write to socket, which is the one to be
         * used when deciding whether we can stop
         */
        uint32_t num_data_cells_added;

        /* number of cells we have sent since the beginning of the
         * defense. to be like CS-BuFLO, even if the socket write()
         * rejects wholy or partially our write, we will still count
         * as a cell written. essentially, this is the number of
         * ATTEMPTS to write to socket, or approximately the number of
         * timer fires
         *
         * this is the values to use when deciding whether can stop
         */
        uint32_t num_write_attempts;

        /* absolute time defense allowed to stay active until, in case
         * user forgets to stop us.
         *
         * if we reach this then it's most likely our bug, or user is
         * loading a huge page/network is really congested; for now we
         * assume it's a bug
         */
        struct timeval auto_stop_time_point;
        /* whether the user has requested that we stopped. we have
         * to continue until to satisfy L pameter */
        bool stop_requested;

        bool need_start_flag_in_next_cell;

        /* as soon as user requests to stop (we must be csp), we will
         * want to immediately notify ssp as well: we set this flag to
         * true. in the code that adds cells to the cell outbuf, we
         * will add the flag and once done, will clear this to
         * false */
        bool need_stop_flag_in_next_cell;

        /* used by ssp to notify csp that it has auto-matically
         * stopped its side of defense session. the csp can choose to
         * tell ssp to start again
         */
        bool need_auto_stopped_flag_in_next_cell;
    } defense_info_;

    // Timer::UniquePtr buflo_timer_;
    bool whole_dummy_cell_at_end_outbuf_;
    /* "whole_dummy_cell_at_end_outbuf_has_important_flags_" is only
     * meaningful when "whole_dummy_cell_at_end_outbuf_" is true,
     * i.e., its value doesn't need to be anything in particular if
     * "whole_dummy_cell_at_end_outbuf_" is false */
    bool whole_dummy_cell_at_end_outbuf_has_important_flags_;

    /* count of those we really avoided, i.e., that are not to be
     * immediately replaced by a dummy cell.... something we have to
     * do sometimes if we want to send a flag but there's no data to
     * piggy-back on
     */
    uint32_t num_dummy_cells_avoided_;

    spdylay_session* spdysess_;

    // buffers data for spdy to read and data spdy wants to write
    struct evbuffer* spdy_inbuf_;
    struct evbuffer* spdy_outbuf_;

    struct evbuffer* peer_info_inbuf_;
    struct evbuffer* my_peer_info_outbuf_;
    // cell in/out bufs are for data that we read from/write into
    // socket
    struct evbuffer* cell_inbuf_;
    struct evbuffer* cell_outbuf_;

    enum CellType : uint8_t
    {
        DATA,
        DUMMY,
        CONTROL,
    };

    // the state we're in for processing the input.
    enum class ReadState
    {
        READ_HEADER = 0, READ_BODY = 1
    };

    struct {
        ReadState state_;
        uint8_t cell_type_;
        uint8_t cell_flags_;
        uint16_t payload_len_;

        void reset()
        {
            state_ = ReadState::READ_HEADER;
            payload_len_ = 0;
            cell_type_ = CellType::DATA;
            cell_flags_ = 0;
        }
    } cell_read_info_;
    bool need_to_read_peer_info_;

    /* number of consecutive cells with DEFENSIVE flag that we're
     * receiving from peer; i.e., cleared to zero whenever a cell has
     * that flag off, and incremented whenever a cell has that flag on
     */
    uint32_t num_consecutive_defensive_cells_from_peer_;

    std::map<int, std::unique_ptr<StreamState> > stream_states_;

    /* number of bytes we have received, of any type, i.e., everything
     * we read from the socket
     */
    uint64_t all_recv_byte_count_;

    /* number of bytes we have received that are user's data, i.e.,
     * data that we will send to user, a.k.a "outward data", for all
     * user connections
     */
    uint64_t all_users_data_recv_byte_count_;

    uint32_t dummy_recv_cell_count_;


    // how much of the cell at front of cell_outbuf_ we have written
    // into socket
    size_t front_cell_sent_progress_;
    /* describes how much useful data is contained in the cells that
     * are in the cell_outbuf_ */
    std::deque<uint16_t> output_cells_data_bytes_info_;

    uint64_t all_send_byte_count_;
    uint64_t all_users_data_send_byte_count_;
    uint32_t dummy_send_cell_count_;

};

}
}

#endif /* bulfo_mux_channel_impl_spdy_hpp */
