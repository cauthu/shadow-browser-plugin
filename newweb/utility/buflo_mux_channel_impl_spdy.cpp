
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <string>
#include <bitset>

#include "buflo_mux_channel_impl_spdy.hpp"
#include "common.hpp"


using std::string;
using std::vector;
using std::pair;
using std::bitset;

#define _LOG_PREFIX(inst) << "buflomux= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)

/*
  cells are fixed size. each contains a header and a body. the header
  has 1-byte "type-and-flags" field and a 2-byte length field. the
  length field specifies the number of useful data bytes at the front
  of the body, and the remaining bytes at the end of the body, if any,
  are dummy bytes.

  the type-and-flags field contains CELL_TYPE_WIDTH bits for the cell
  type, and CELL_FLAGS_WIDTH bits for the flags.

  this way, when the client side sends a data cell and also wants to
  tell the server side to activate defense, it can include approparite
  flags in the data cell. instead of having to send a separate control
  cell. this is an optimization.

  similarly, when the client wants to tell server to stop defending,
  it can include a flag in a data cell, saving it from having to send
  a separate control cell. however, to make the most of this, a cell
  should be not written to the cell_outbuf_ until the last possible
  moment, so that the cell's flags can be updated as late as
  possible. for example, a data cell is prepared WITHOUT the STOP flag
  and put into the cell_outbuf_. but the network is heavily is
  congested and we can't write any data into socket for a while. then
  once we are able to write, enough time has elapsed that we NOW WANT
  to tell ssp to stop, but because cell has been put into the
  cell_outbuf_ it's hard to update its flags.

  UPDATE!!!! the above is no longer true for two reasons: 1) we are
  keeping track of the progress of individual cells inside the
  cell_outbuf_ (originally for stats purposes), so we can know where
  the cell boundaries are, and thus can update the flags if needed, 2)
  say the ssp auto-stops, and it flushes and thus there are hundreds
  of cells in the cell outbuf, then csp quickly tells ssp to 'start'
  defense again, so ssp starts defense, but it still has a lot of
  cells queued up in the cell outbuf. then any new control info it
  needs to send will be queued behind all those cells. however right
  now the only control flag ssp needs to send is AUTO_STOPPED, and it
  only does that when it stops the defense, which means it will send
  as quick as possible, so even if the auto_stopped is stuck behind a
  bunch of cells (e.g., this is the second time it has to auto stop
  during a page load) it won't have to wait too long.


  XXX/the way we have it right now, we don't really need the DUMMY
  cell type because we can just have a DATA cell with a zero payload
  length, which is equivalent to a fully dummy cell. having a separate
  dummy cell type is useful if, for example, for a dummy cell, we can
  skip the length field


  cell_outbuf_ contains the bytes that are sent into the socket.

  in steady state of defense mode, the cell_outbuf_ should NEVER
  contain two full cells; it might contain part of a cell that has
  been partially written to the socket, followed by one full cell,
  which is to ensure that when we want to write into the socket the
  next time, there is always at least one full cell's worth of data to
  write.


  ---------
  Logic on CSP to know when SSP has finished defending the downstream
  direction:

  CSP cannot rely solely on the number of consecutive "defensive"
  cells it receives from the SSP: it's because the SSP can drop dummy
  cells (e.g., due to congestion), so the number of cells CSP receives
  might not match the number of "send attempts" SSP makes and thus
  might not reach a multiple-of-L (e.g., if L=50, CSP might only
  receive 48 defensive cells because the SSP drops 2 dummy cells).

  so, in the case where the SSP drops dummy cells, one solution to
  above problem is for SSP to send an explicit signal that it has done
  defending its send direction (i.e., the CSP's receive direction)

  so, instead of behaving differently in different cases, for
  simplicity, i'll go with the solution of using the explicit "done"
  signal in all cases: the SSP will send a cell (data or dummy) with
  the flag "done" set to explicitly tell CSP that the SSP is done
  defending the downstream direction.

*/

static const uint8_t s_version = 9;

/* 1 byte for version, 2 for cell size, and 4 for address, 2 for
 * requested L, 2 for requested pkt interval.
 *
 * the requested L is only used by CSP to tell SSP what L to use,
 * i.e., override SSP's default L. the SSP never sends this field,
 * i.e., always zero.
 */
#define PEER_INFO_NUM_BYTES (1 + 2 + 4 + 2 + 2)

// sizes in bytes
#define CELL_TYPE_AND_FLAGS_FIELD_SIZE 1
#define CELL_PAYLOAD_LEN_FIELD_SIZE 2
#define CELL_HEADER_SIZE ((CELL_TYPE_AND_FLAGS_FIELD_SIZE) + (CELL_PAYLOAD_LEN_FIELD_SIZE))


// in bits
#define CELL_TYPE_WIDTH 3
#define CELL_FLAGS_WIDTH (8 - CELL_TYPE_WIDTH)
#define CELL_TYPE_MASK ((unsigned(~0)) << CELL_FLAGS_WIDTH)
#define CELL_TYPE_SHIFT_AMT CELL_FLAGS_WIDTH

static_assert((CELL_TYPE_WIDTH + CELL_FLAGS_WIDTH) == 8,
              "must be one byte");


// flags bit positions in cell flags
#define CELL_FLAGS_START_DEFENSE_POSITION 0
#define CELL_FLAGS_STOP_DEFENSE_POSITION 1

// only the ssp set these flags
#define CELL_FLAGS_DEFENSE_AUTO_STOPPED_POSITION 2
#define CELL_FLAGS_DEFENSE_DONE_POSITION 3

// this flag is set for "defensive" cells, i.e., those that are sent
// when the sender is in an active defense session.
//
// both sides (csp and ssp) can use this flag
#define CELL_FLAGS_DEFENSIVE_POSITION 4

static_assert(CELL_FLAGS_START_DEFENSE_POSITION < CELL_FLAGS_WIDTH,
              "bit position out-of-bounds");
static_assert(CELL_FLAGS_STOP_DEFENSE_POSITION < CELL_FLAGS_WIDTH,
              "bit position out-of-bounds");
static_assert(CELL_FLAGS_DEFENSE_AUTO_STOPPED_POSITION < CELL_FLAGS_WIDTH,
              "bit position out-of-bounds");
static_assert(CELL_FLAGS_DEFENSE_DONE_POSITION < CELL_FLAGS_WIDTH,
              "bit position out-of-bounds");
static_assert(CELL_FLAGS_DEFENSIVE_POSITION < CELL_FLAGS_WIDTH,
              "bit position out-of-bounds");

/* extract the type from the type_and_flags field t_n_f */
#define GET_CELL_TYPE(t_n_f) \
    (((t_n_f) & CELL_TYPE_MASK) >> CELL_TYPE_SHIFT_AMT)

/* set the type into the type_and_flags field t_n_f, without affecting
 * current flags bits */
#define SET_CELL_TYPE(t_n_f, type)                                      \
    do {                                                                \
        /* first, clear out the type bits in t_n_f */                   \
        (t_n_f) &= ~CELL_TYPE_MASK;                                     \
        /* left shift the type by CELL_TYPE_SHIFT_AMT, then or the */   \
        /* result with the current t_n_f */                             \
        (t_n_f) |= (type) << CELL_TYPE_SHIFT_AMT;                       \
    } while (0)

/* extract the flags from the type_and_flags field t_n_f */
#define GET_CELL_FLAGS(t_n_f) \
    ((t_n_f) & ~CELL_TYPE_MASK)

/* set the flags into the type_and_flags field t_n_f, without
 * affecting current type bits. the flags is assumed to be in-range */
#define SET_CELL_FLAGS(t_n_f, flags)                            \
    do {                                                        \
        /* first clear out the flags bits in t_n_f */           \
        (t_n_f) &= CELL_TYPE_MASK;                              \
        /* or the flags into the t_n_f */                       \
        (t_n_f) |= (flags);                                     \
    } while (0)


#define MAYBE_GET_STREAMSTATE(sid, reset_stream_if_unknown, errretval)  \
    auto streamstate = stream_states_[sid].get();                       \
    do {                                                                \
        if (!streamstate) {                                             \
            /* std::map just created an empty unique_ptr */             \
            /* so we erase it */                                        \
            logself(WARNING) << "unknown sid: " << sid;                 \
            stream_states_.erase(sid);                                  \
            if (reset_stream_if_unknown) {                              \
                vlogself(2) << "resetting sid: " << sid;                \
                auto rv = spdylay_submit_rst_stream(                    \
                    spdysess_, sid, SPDYLAY_CANCEL);                    \
                CHECK_EQ(rv, 0);                                        \
            }                                                           \
            return errretval;                                           \
        }                                                               \
    } while(0)

#define SET_CELL_START_FLAG(t_n_f)                                  \
    do {                                                            \
        s_set_cell_flag(t_n_f, CELL_FLAGS_START_DEFENSE_POSITION);  \
    } while (0)

#define SET_CELL_STOP_FLAG(t_n_f)                                   \
    do {                                                            \
        s_set_cell_flag(t_n_f, CELL_FLAGS_STOP_DEFENSE_POSITION);   \
    } while (0)

#define SET_CELL_AUTO_STOPPED_FLAG(t_n_f)                               \
    do {                                                                \
        s_set_cell_flag(t_n_f, CELL_FLAGS_DEFENSE_AUTO_STOPPED_POSITION); \
    } while (0)

#define SET_CELL_DONE_FLAG(t_n_f)                                       \
    do {                                                                \
        s_set_cell_flag(t_n_f, CELL_FLAGS_DEFENSE_DONE_POSITION);       \
    } while (0)

#define SET_CELL_DEFENSIVE_FLAG(t_n_f)                          \
    do {                                                        \
        s_set_cell_flag(t_n_f, CELL_FLAGS_DEFENSIVE_POSITION);  \
    } while (0)


static void
_self_test_bit_manipulation();

/*
 * essentially this affects only the ONE bit at the "flag_pos", i.e.,
 * any other bits that might already be set will remain set in the
 * result
 */
static inline void
s_set_cell_flag(uint8_t* t_n_f, const uint8_t flag_pos)
{
    /* !!! initialize the flags_bs with the CURRENT FLAGS that might
     * already be set !!!
     */
    bitset<CELL_FLAGS_WIDTH> flags_bs(GET_CELL_FLAGS(*t_n_f));
    flags_bs.set(flag_pos, true);

    const auto flags_val = flags_bs.to_ulong();
    SET_CELL_FLAGS(*t_n_f, flags_val);
}


namespace myio { namespace buflo
{

BufloMuxChannelImplSpdy::BufloMuxChannelImplSpdy(
    struct event_base* evbase,
    int fd, bool is_client_side, const in_addr_t& myaddr,
    size_t cell_size, const uint32_t& tamaraw_pkt_intvl_ms,
    const uint32_t& peer_tamaraw_pkt_intvl_ms,
    const uint32_t& tamaraw_L,
    const uint32_t& defense_session_time_limit,
    ChannelStatusCb ch_status_cb,
    NewStreamConnectRequestCb st_connect_req_cb)
    : BufloMuxChannel(fd, is_client_side,
                      ch_status_cb,
                      st_connect_req_cb)
    , evbase_(evbase)
    , socket_read_ev_(nullptr, event_free)
    , socket_write_ev_(nullptr, event_free)
    , cell_size_(cell_size)
    , tamaraw_pkt_intvl_ms_(tamaraw_pkt_intvl_ms)
    , peer_tamaraw_pkt_intvl_ms_(peer_tamaraw_pkt_intvl_ms)
    , tamaraw_L_(tamaraw_L)
    , cell_body_size_(cell_size_ - (CELL_HEADER_SIZE))
    , peer_cell_size_(0)
    , peer_cell_body_size_(0)
    , myaddr_(myaddr)
    , defense_session_time_limit_(defense_session_time_limit)
    , whole_dummy_cell_at_end_outbuf_(false)
    , num_dummy_cells_avoided_(0)
    , front_cell_sent_progress_(0)
    , need_to_read_peer_info_(true)
    , all_recv_byte_count_(0)
    , all_users_data_recv_byte_count_(0)
    , dummy_recv_cell_count_(0)
    , all_send_byte_count_(0)
    , all_users_data_send_byte_count_(0)
    , dummy_send_cell_count_(0)
{
    /* the value used by tamaraw paper */
    CHECK((cell_size == 750)
          || (cell_size == 0));

    if (defense_session_time_limit_) {
        CHECK_LE(defense_session_time_limit_, 60 * 3);
    }

    CHECK(_check_L(tamaraw_L_)) << "bad L: " << tamaraw_L_;

    CHECK(_check_pkt_intvl(tamaraw_pkt_intvl_ms_))
        << "bad pkt interval: " << tamaraw_pkt_intvl_ms_;

    if (tamaraw_L_ == 0 || tamaraw_pkt_intvl_ms_ == 0) {
        // if either is 0, then both must be 0
        CHECK((tamaraw_L == 0) && (tamaraw_pkt_intvl_ms_ == 0));
    }

    if (peer_tamaraw_pkt_intvl_ms_) {
        CHECK(is_client_side_);
        CHECK(_check_pkt_intvl(peer_tamaraw_pkt_intvl_ms_));
    }

    if (cell_size_ || tamaraw_L_ || tamaraw_pkt_intvl_ms_ || defense_session_time_limit_)
    {
        // if using any of these, then all have to be specified
        CHECK(cell_size_ && tamaraw_L_ && tamaraw_pkt_intvl_ms_ && defense_session_time_limit_);
    }


    logself(INFO) << "my version= " << unsigned(s_version)
                  << " using cell size= " << cell_size_
                  << " interval= " << tamaraw_pkt_intvl_ms_
                  << " L= " << tamaraw_L_
                  << " time limit= " << defense_session_time_limit_;

    defense_info_.reset();

    /* use highest priority, which is 0, for the buflo timer */
    buflo_timer_.reset(
        new Timer(evbase, false,
                  boost::bind(&BufloMuxChannelImplSpdy::_buflo_timer_fired,
                              this, _1),
                  0));

    _setup_spdylay_session();

#define ALLOC_EVBUF(buf) \
    do { buf = evbuffer_new(); CHECK_NOTNULL(buf); } while (0)

    ALLOC_EVBUF(spdy_inbuf_);
    ALLOC_EVBUF(spdy_outbuf_);
    ALLOC_EVBUF(peer_info_inbuf_);
    ALLOC_EVBUF(my_peer_info_outbuf_);
    ALLOC_EVBUF(cell_inbuf_);
    ALLOC_EVBUF(cell_outbuf_);

#undef ALLOC_EVBUF

    cell_read_info_.reset();

    socket_read_ev_.reset(
        event_new(evbase_, fd_, EV_READ | EV_PERSIST, s_socket_readcb, this));
    socket_write_ev_.reset(
        event_new(evbase_, fd_, EV_WRITE | EV_PERSIST, s_socket_writecb, this));

    // the write event has to be enabled only when we have data to
    // write

    _fill_my_peer_info_outbuf();

    auto rv = 0;
    if (is_client_side_) {
        // client cand send my info now
        //
        // server first waits to read hello from client; otherwise our
        // data might arrive right behind the socks5 reponse, which
        // can confuse our csp when it tells the tcp channel to
        // release the fd (see issue #4
        // https://bitbucket.org/hatswitch/shadow-plugin-extras/issues/4/)
        _write_my_peer_info_outbuf();
        CHECK(!my_peer_info_outbuf_);
    }

    /* poll to check for socket close
     *
     * we want to use EV_WRITE to be notified that socket is
     * closed... but shadow doesn't support edge-triggered event, so
     * we will repeatedly get the EV_WRITE event for an idle
     * socket. so we can't use it; same reason we have to use
     * _maybe_toggle_write_monitoring() -- lack of edge-triggered
     * event support. so we use a polling method: set a time out on
     * the read event, and we try to read on timeout, and it should
     * return no bytes
     */
    struct timeval timeout_tv;
    timeout_tv.tv_sec = 5;
    timeout_tv.tv_usec = 0;

#ifdef IN_SHADOW
        const auto timeout_tv_ptr = &timeout_tv;
#else
        const auto timeout_tv_ptr = nullptr;
#endif

    rv = event_add(socket_read_ev_.get(), timeout_tv_ptr);
    CHECK_EQ(rv, 0);

    _self_test_bit_manipulation();
}

int
BufloMuxChannelImplSpdy::create_stream(const char* host,
                                       const in_port_t& port,
                                       void *cbdata)
{
    LOG(FATAL) << "not yet implemented";
    return 0;
}

int
BufloMuxChannelImplSpdy::create_stream2(const char* host,
                                       const in_port_t& port,
                                       BufloMuxChannelStreamObserver *observer)
{
    vlogself(2) << "begin";

    string hostport_str(host);
    hostport_str.append(":");
    hostport_str.append(std::to_string(port));

    const char *nv[] = {
        ":host", hostport_str.c_str(),
        nullptr};

    /* spdylay will make copies of nv */
    int rv = spdylay_submit_syn_stream(spdysess_, 0, 0, 0, nv, observer);
    CHECK_EQ(rv, 0);

    _pump_spdy_send();

    vlogself(2) << "done";
    return 0;
}

bool
BufloMuxChannelImplSpdy::start_defense_session()
{
    CHECK_EQ(defense_info_.state, DefenseState::NONE)
        << "currently only support starting session when none is active";

    // both must be greather than 0
    CHECK_GT(tamaraw_pkt_intvl_ms_, 0);
    CHECK_GT(tamaraw_L_, 0);

    struct timeval current_tv;
    auto rv = gettimeofday(&current_tv, nullptr);
    CHECK_EQ(rv, 0);

    /* if we're on the server side, it's possible due to congestion
     * that the client's stop request doesn't reach us in team, so
     * wait a little longer than on client side
     */
    struct timeval duration_tv = {0};
    duration_tv.tv_sec = defense_session_time_limit_;

    evutil_timeradd(&current_tv, &duration_tv, &defense_info_.auto_stop_time_point);

    struct timeval intvl_tv = {0};
    intvl_tv.tv_sec = 0;
    intvl_tv.tv_usec = (tamaraw_pkt_intvl_ms_ * 1000);

    buflo_timer_->start(&intvl_tv);

    defense_info_.state = DefenseState::ACTIVE;

    /* force disable the write event because we will write when timer
     * fires */
    _maybe_toggle_write_monitoring(ForceToggleMode::FORCE_DISABLE);

    logself(INFO) << "defense started";

    return true;
}

void
BufloMuxChannelImplSpdy::set_auto_start_defense_session_on_next_send()
{
    CHECK_EQ(defense_info_.state, DefenseState::NONE);
    CHECK(is_client_side_);

    // data outbuf and cell_outbuf_must be empty
    CHECK_EQ(evbuffer_get_length(spdy_outbuf_), 0);
    CHECK_EQ(evbuffer_get_length(cell_outbuf_), 0);

    defense_info_.state = DefenseState::PENDING_NEXT_SOCKET_SEND;
    CHECK(!defense_info_.need_start_flag_in_next_cell);
    defense_info_.need_start_flag_in_next_cell = true;
}

void
BufloMuxChannelImplSpdy::stop_defense_session(bool right_now)
{
    logself(INFO) << "requested to stop defense; "
                  << "current number of defensive cells sent/attempted: "
                  << defense_info_.num_write_attempts;

    if (defense_info_.state != DefenseState::ACTIVE) {
        logself(INFO) << "defense not currently active, so do nothing";
        defense_info_.reset();
        return;
    }

    if (!right_now) {
        defense_info_.request_stop();

        if (is_client_side_) {
            defense_info_.need_stop_flag_in_next_cell = true;
        }
    } else {
        CHECK(0) << "todo";
    }
}

bool
BufloMuxChannelImplSpdy::set_stream_observer(int sid,
                                             BufloMuxChannelStreamObserver* observer)
{
    const auto rv = spdylay_session_set_stream_user_data(
        spdysess_, sid, observer);
    return (rv == 0);
}

bool
BufloMuxChannelImplSpdy::set_stream_connected(int sid)
{
    static const char *nv[] = {
        "ok", "true",
        nullptr,
    };
    // don't use flag_fin because it would mean this would be the last
    // frame sent on this stream
    auto rv = spdylay_submit_syn_reply(spdysess_, 0, sid, nv);
    CHECK_EQ(rv, 0);

    _init_stream_data_provider(sid);

    _pump_spdy_send();

    return true;
}

int
BufloMuxChannelImplSpdy::read_buffer(int sid, struct evbuffer* buf, size_t len)
{
    MAYBE_GET_STREAMSTATE(sid, false, -1);

    const auto rv = evbuffer_remove_buffer(
        /* src */ streamstate->outward_buf_, /* dst */ buf, len);
    return rv;
}

int
BufloMuxChannelImplSpdy::drain(int sid, size_t len) 
{
    logself(FATAL) << "to implement";
    return 0;
}

uint8_t*
BufloMuxChannelImplSpdy::peek(int sid, ssize_t len) 
{
    logself(FATAL) << "to implement";
    return nullptr;
}

struct evbuffer*
BufloMuxChannelImplSpdy::get_input_evbuf(int sid)
{
    MAYBE_GET_STREAMSTATE(sid, false, nullptr);

    // return the buf that user can read their new input data, i.e.,
    // outward data.  XXX/maybe rename function

    return streamstate->outward_buf_;
}

size_t
BufloMuxChannelImplSpdy::get_avail_input_length(int sid) const
{
    const auto it = stream_states_.find(sid);
    if (it != stream_states_.end()) {
        return evbuffer_get_length(it->second->outward_buf_);
    } else {
        return 0;
    }
}

int
BufloMuxChannelImplSpdy::write_buffer(int sid, struct evbuffer *buf) 
{
    MAYBE_GET_STREAMSTATE(sid, false, -1);

    auto rv = evbuffer_add_buffer(
        /* dst */ streamstate->inward_buf_,
        /* src */ buf);
    CHECK_EQ(rv, 0);

    if (streamstate->inward_deferred_) {
        vlogself(2) << "inner stream " << sid << " was deferred; resume now";
        rv = spdylay_session_resume_data(spdysess_, sid);
        CHECK_EQ(rv, 0);
        streamstate->inward_deferred_ = false;
    }

    _pump_spdy_send();

    return 0;
}

int
BufloMuxChannelImplSpdy::set_write_eof(int sid) 
{
    MAYBE_GET_STREAMSTATE(sid, false, -1);

    vlogself(2) << "begin";

    CHECK(!streamstate->inward_has_seen_eof_);
    streamstate->inward_has_seen_eof_ = true;

    if (streamstate->inward_deferred_) {
        vlogself(2) << "inner stream " << sid << " was deferred; resume now";
        const auto rv = spdylay_session_resume_data(spdysess_, sid);
        CHECK_EQ(rv, 0);
        streamstate->inward_deferred_ = false;
    }

    _pump_spdy_send();

    vlogself(2) << "done";

    return 0;
}

void
BufloMuxChannelImplSpdy::close_stream(int sid) 
{
    /*
     * if the observer triggers stream closure, either by calling this
     * directly, or via some other method, then assume that we don't
     * want to notify him of the closure because presumably he might
     * have been destroyed. so we clear the observer here first before
     * resetting the stream
     */

    vlogself(2) << "RESET stream " << sid;

    // first, clear the observer
    set_stream_observer(sid, nullptr);

    auto rv = spdylay_submit_rst_stream(spdysess_, sid, SPDYLAY_CANCEL);
    CHECK_EQ(rv, 0);

    _pump_spdy_send();

    // we don't clean up things like stream state here; instead will
    // do in on_stream_close_callback
}

void
BufloMuxChannelImplSpdy::_buflo_timer_fired(Timer* timer)
{
    CHECK_EQ(defense_info_.state, DefenseState::ACTIVE);

    vlogself(2) << "begin +++";

    struct timeval current_tv;
    const auto rv = gettimeofday(&current_tv, nullptr);
    CHECK_EQ(rv, 0);

    if (defense_info_.is_done_defending_send(tamaraw_L_)) {
        logself(INFO) << "done defending send; defensive cells sent/attempted= "
                      << defense_info_.num_write_attempts;
        defense_info_.saved_num_write_attempts = defense_info_.num_write_attempts;
        buflo_timer_->cancel();

        // we are finally done according to normal operation

        /* reset so state becomes none so _pump_spdy_send() will
         * flush. but need to save and restore the
         * need_stop_flag_in_next_cell
         */

        const auto saved_bool = defense_info_.need_stop_flag_in_next_cell;
        defense_info_.reset();
        defense_info_.need_stop_flag_in_next_cell = saved_bool;

        if (!is_client_side_) {
            // ssp tells csp that it's done defending its send
            // direction, i.e., csp's receive direction
            defense_info_.need_done_flag_in_next_cell = true;
        }

        /* this will flush data cells and toggle write monitoring
         * appropriately */
        _pump_spdy_send(true);

        if (defense_info_.need_stop_flag_in_next_cell || defense_info_.need_done_flag_in_next_cell)
        {
            vlogself(2) << "still need to send the stop/done flag, so we send a dummy cell";
            /* the flag could not piggyback on any cell, so we have to
             * add a control/dummy cell ourselves here
             */

            _maybe_drop_whole_dummy_cell_at_end_outbuf(__LINE__, false);
            CHECK(!whole_dummy_cell_at_end_outbuf_);

            _add_ONE_dummy_cell_to_outbuf();
            _maybe_toggle_write_monitoring(ForceToggleMode::FORCE_ENABLE);
        } else {
            vlogself(2) << "the stop flag has been added";
        }

        CHECK(!defense_info_.need_stop_flag_in_next_cell);
        CHECK(!defense_info_.need_done_flag_in_next_cell);

        _check_notify_a_defense_session_done(__LINE__);

        goto done;
    } else {
        if (evutil_timercmp(&current_tv, &defense_info_.auto_stop_time_point, >=)) {
            if (is_client_side_) {
                logself(FATAL) << "exceeding defense session time limit! "
                               << "perhaps you forgot to stop defense after "
                               << "you're done with a page load?";
            } else {
                // we're on ssp
                logself(WARNING) << "exceeding defense session time limit! "
                                 << "auto-stopping; number of defensive cells sent/attempted: "
                                 << defense_info_.num_write_attempts;
                defense_info_.saved_num_write_attempts = defense_info_.num_write_attempts;
                buflo_timer_->cancel();

                defense_info_.reset();
                defense_info_.need_auto_stopped_flag_in_next_cell = true;

                _pump_spdy_send(true);

                if (defense_info_.need_auto_stopped_flag_in_next_cell) {
                    /* the flag could not piggyback on any cell, so we
                     * have to add a control/dummy cell ourselves here
                     */

                    _maybe_drop_whole_dummy_cell_at_end_outbuf(__LINE__, false);
                    CHECK(!whole_dummy_cell_at_end_outbuf_);

                    _add_ONE_dummy_cell_to_outbuf();
                    _maybe_toggle_write_monitoring(ForceToggleMode::FORCE_ENABLE);
                }

                CHECK(!defense_info_.need_auto_stopped_flag_in_next_cell);

                goto done;
            }
        }
    }

    vlogself(2) << "defense is still on-going";

    if (evbuffer_get_length(cell_outbuf_) >= cell_size_) {
        // there is already one or more cell's worth of bytes waiting
        // to be sent, so we just try to send it and return
        _send_cell_outbuf();
        goto done;
    }

    // need to add another cell to cell outbuf

    if (!_maybe_add_ONE_data_cell_to_outbuf()) {
        // could not add data, so add dummy
        _ensure_a_whole_dummy_cell_at_end_outbuf();
    }

    // there must be at least one cell's worth of bytes in the outbuf
    CHECK_GE(evbuffer_get_length(cell_outbuf_), cell_size_);

    _send_cell_outbuf();

done:
    vlogself(2) << "done ---";
}

/*
 * after telling spdy to write to its buf, if there is defense, then
 * FLUSH all spdy data to cell outbuf (i.e., call
 * _maybe_flush_data_to_cell_outbuf()) and will enable write
 * monitoring so we can actually send to peer asap
 */
void
BufloMuxChannelImplSpdy::_pump_spdy_send(const bool log_flushed_cell_count)
{
    vlogself(2) << "begin";

    // tell spdy session to send -- it will call our send_cb
    spdylay_session* session = spdysess_;
    auto rv = spdylay_session_send(session);
    if (rv) {
        if (SPDYLAY_ERR_EOF == rv) {
            vlogself(1) << "remote peer closed";
        } else {
            logself(WARNING) << "spdylay_session_send() returned \""
                             << spdylay_strerror(rv) << "\"";
        }
        CHECK(0) << "todo";
        return;
    }

    if (!cell_size_) {
        vlogself(2) << "we're not using cells";
        _maybe_toggle_write_monitoring(ForceToggleMode::NONE);
        return;
    }

    size_t num_cells_added = 0;

    if (defense_info_.state == DefenseState::NONE) {
        vlogself(2) << "maybe flush to cell outbuf";
        num_cells_added = _maybe_flush_data_to_cell_outbuf();
        if (num_cells_added) {
            // there is definitely in out buf so just force enable
            _maybe_toggle_write_monitoring(ForceToggleMode::FORCE_ENABLE);
        }
        if (log_flushed_cell_count && num_cells_added) {
            logself(INFO) << "added " << num_cells_added << " data cells";
        }
    } else if (defense_info_.state == DefenseState::PENDING_NEXT_SOCKET_SEND) {
        vlogself(2) << "we want to add only one cell";
        num_cells_added = _maybe_add_ONE_data_cell_to_outbuf();
        // must have added
        CHECK(num_cells_added == 1) << "num_cells_added: " << num_cells_added;
        _maybe_toggle_write_monitoring(ForceToggleMode::FORCE_ENABLE);
    } else {
        vlogself(2) << "defense is active, we don't enable write monitoring";
        CHECK_EQ(defense_info_.state, DefenseState::ACTIVE);
    }

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::_pump_spdy_recv()
{
    vlogself(2) << "begin";

    // tell spdy session to send -- it will call our send_cb
    spdylay_session* session = spdysess_;
    auto rv = spdylay_session_recv(session);
    if (rv) {
        if (SPDYLAY_ERR_EOF == rv) {
            vlogself(1) << "remote peer closed";
        } else {
            logself(WARNING) << "spdylay_session_recv() returned \""
                             << spdylay_strerror(rv) << "\"";
        }
        CHECK(0) << "todo";
        return;
    }

    vlogself(2) << "done";
}

/* will call _maybe_add_ONE_data_cell_to_outbuf()
 *
 * returns the number of cells that we added to outbuf
 */
size_t
BufloMuxChannelImplSpdy::_maybe_flush_data_to_cell_outbuf()
{
    CHECK(   (defense_info_.state == DefenseState::NONE)
        );
    size_t num_added = 0;
    vlogself(2) << "begin, spdy outbuf len: "
                << evbuffer_get_length(spdy_outbuf_);

    while (evbuffer_get_length(spdy_outbuf_)) {
        const auto rv = _maybe_add_ONE_data_cell_to_outbuf();
        CHECK(rv);
        ++num_added;
    }
    vlogself(2) << "done, returning " << num_added;
    return num_added;
}

/*
 * currently we have 4 possible flags.
 *
 * the DEFENSIVE flag is redundant for dummy cells, i.e., that's the
 * purpose of dummy cells: to defend
 *
 * but the other 3 flags -- START, STOP, and AUTO_STOPPED -- might be
 * part of a dummy cell (the START flag might be attached to a dummy
 * cell to tell ssp to start again after it has auto-stopped). and
 * dummy cells might be dropped due to congestion, so we need a way to
 * prevent such dummy cells from being dropped (although there's a
 * more optimal way: if the dummy cell is dropped to make way for a
 * data cell, then we can somehow make that data cell carry the flags
 * of the dropped dummy cell)
 */

bool
BufloMuxChannelImplSpdy::_maybe_set_cell_flags(uint8_t* type_n_flags,
                                               const char* cell_type)
{
    bool has_important_flags = false;

    if (defense_info_.need_start_flag_in_next_cell) {
        /* we should need the start flag only when waiting for first
         * socket write or when we're active and has not been
         * requested to stop (presumably because the ssp has
         * auto-stopped and we want it to start again) */
        CHECK((defense_info_.state == DefenseState::PENDING_NEXT_SOCKET_SEND)
              || (defense_info_.state == DefenseState::ACTIVE && !defense_info_.stop_requested));
        CHECK(is_client_side_);
        vlogself(1) << "setting the START flag, in a " << cell_type << " cell";
        SET_CELL_START_FLAG(type_n_flags);
        defense_info_.need_start_flag_in_next_cell = false;
        has_important_flags = true;
    }

    if (defense_info_.need_stop_flag_in_next_cell) {
        // CHECK(defense_info_.stop_requested);
        vlogself(1) << "setting the STOP flag, in a " << cell_type << " cell";
        SET_CELL_STOP_FLAG(type_n_flags);
        defense_info_.need_stop_flag_in_next_cell = false;
        has_important_flags = true;
    }

    if (defense_info_.need_auto_stopped_flag_in_next_cell) {
        CHECK(!is_client_side_);
        vlogself(1) << "setting the AUTO_STOPPED flag, in a " << cell_type << " cell";
        SET_CELL_AUTO_STOPPED_FLAG(type_n_flags);
        defense_info_.need_auto_stopped_flag_in_next_cell = false;
        has_important_flags = true;
    }

    if (defense_info_.need_done_flag_in_next_cell) {
        CHECK(!is_client_side_);
        vlogself(1) << "setting the DONE flag, in a " << cell_type << " cell";
        SET_CELL_DONE_FLAG(type_n_flags);
        defense_info_.need_done_flag_in_next_cell = false;
        has_important_flags = true;
    }

    if ((defense_info_.state == DefenseState::ACTIVE)
        || (defense_info_.state == DefenseState::PENDING_NEXT_SOCKET_SEND))
    {
        SET_CELL_DEFENSIVE_FLAG(type_n_flags);
    }

    return has_important_flags;
}

/*
 * possibly add ONE cell of spdy data to the cell outbuf
 */
bool
BufloMuxChannelImplSpdy::_maybe_add_ONE_data_cell_to_outbuf()
{
    // static const uint8_t type_field = CellType::DATA;

    /* append a data cell if there's data in spdy_outbuf_ */

    const size_t payload_len = std::min(
        cell_body_size_,
        evbuffer_get_length(spdy_outbuf_));
    const uint16_t len_field = htons(payload_len);

    if (payload_len == 0) {
        return false;
    }

    /*
     * there is data so we do want to add
     */

    // first, maybe we can drop a whole dummy cell
    if (_maybe_drop_whole_dummy_cell_at_end_outbuf(__LINE__)) {
        vlogself(2) << "replacing a dummy cell with a data cell";
    }

    CHECK(!whole_dummy_cell_at_end_outbuf_);

    // add type and length
    uint8_t type_n_flags = 0;
    SET_CELL_TYPE(type_n_flags, CellType::DATA);

    _maybe_set_cell_flags(&type_n_flags, "data");

    auto rv = evbuffer_add(
        cell_outbuf_, (const uint8_t*)&type_n_flags, sizeof type_n_flags);
    CHECK_EQ(rv, 0);
    rv = evbuffer_add(
        cell_outbuf_, (const uint8_t*)&len_field, sizeof len_field);
    CHECK_EQ(rv, 0);

    // move spdy data into the cell buf
    rv = evbuffer_remove_buffer(spdy_outbuf_, cell_outbuf_, payload_len);
    CHECK_EQ(rv, payload_len);

    vlogself(2) << "added " << payload_len << " bytes of spdy payload";

    // do we need to pad?
    if (cell_body_size_ > payload_len) {
        const auto pad_len = cell_body_size_ - payload_len;
        vlogself(2) << "need to pad the cell body with " << pad_len << " bytes";

        // TODO: if the pad len is small e.g., a few bytes, it is
        // probably be better to copy dummy bytes into the buf instead
        // of adding reference, which definitely requires extra
        // book-keeping by libevent, vs adding a few bytes might not
        // require addtional book-keeping
        rv = evbuffer_add_reference(
            cell_outbuf_, common::static_bytes->c_str(),
            pad_len, nullptr, nullptr);
        CHECK_EQ(rv, 0);
    } else {
        vlogself(2) << "no need for padding";
    }

    if (defense_info_.state == DefenseState::PENDING_NEXT_SOCKET_SEND) {
        ++defense_info_.num_data_cells_added;
        CHECK_EQ(defense_info_.num_data_cells_added, 1);
        // we expect that we are in the pending state only until the
        // first cell has been written and the the state switches to
        // acitive.
    }

    output_cells_data_bytes_info_.push_back(payload_len);

    return true;
}


void
BufloMuxChannelImplSpdy::_update_output_cell_progress(int num_written)
{
    CHECK(num_written > 0) << num_written;

    vlogself(2) << "begin, num_written: " << num_written;

    while (num_written > 0) {
        vlogself(2) << "num_written: " << num_written
                    << " front_cell_sent_progress_: " << front_cell_sent_progress_;

        {
            const size_t new_progress = std::min(
                front_cell_sent_progress_ + num_written, cell_size_);
            CHECK(new_progress > front_cell_sent_progress_);
            const size_t sent_now = new_progress - front_cell_sent_progress_;
            vlogself(2) << "sent just now: " << sent_now;

            num_written = num_written - sent_now;
            front_cell_sent_progress_ = new_progress;
            vlogself(2) << "new front_cell_sent_progress_: " << front_cell_sent_progress_;
        }

        if (front_cell_sent_progress_ == cell_size_) {
            vlogself(2) << "done sending front cell...";
            const auto num_data_bytes_in_front_cell = output_cells_data_bytes_info_.at(0);
            if (num_data_bytes_in_front_cell > 0) {
                vlogself(2) << "   ... with num_data_bytes_in_front_cell= "
                            << num_data_bytes_in_front_cell;
                all_users_data_send_byte_count_ += num_data_bytes_in_front_cell;
            } else {
                vlogself(2) << "   ... it was a whole dummy cell";
                ++dummy_send_cell_count_;
            }

            // reset progress
            front_cell_sent_progress_ = 0;
            output_cells_data_bytes_info_.pop_front();
        } else {
            // do nothing
            CHECK(num_written == 0);
        }
    }

    vlogself(2) << "done";
}

/* will send the appropriate number of bytes based on whether a
 * defense is active or not....
 *
 * does NOT add cells to cell_outbuf_; only tries to write
 * cell_outbuf_ to socket
 */
void
BufloMuxChannelImplSpdy::_send_cell_outbuf()
{
    vlogself(2) << "begin";

    CHECK(cell_size_ > 0);
    auto curbufsize = evbuffer_get_length(cell_outbuf_);

    int num_written = 0;
    bool did_attempt_write = false;

    if (defense_info_.state == DefenseState::ACTIVE) {
        // there must be at least one cell in outbuf
        CHECK_GE(curbufsize, cell_size_);

        vlogself(2) << "tell socket to write ONE cell's worth of bytes";
        num_written = evbuffer_write_atmost(cell_outbuf_, fd_, cell_size_);
        vlogself(2) << "evbuffer_write_atmost() return: " << num_written;

        did_attempt_write = true;

        defense_info_.increment_send_attempt();

        curbufsize = evbuffer_get_length(cell_outbuf_);
        vlogself(2) << "remaining in outbuf: " << curbufsize;

        if (curbufsize < cell_size_) {
            // we can blindly clear this flag: whether or not there
            // was one whole dummy cell, it's no longer true because
            // cur buf size is less than one whole cell
            whole_dummy_cell_at_end_outbuf_ = false;
        } else {
            // there's at least one whole cell remaining in out buf
            if (whole_dummy_cell_at_end_outbuf_) {
                // we logically want to drop this whole dummy cell at
                // the end of out buf, but to avoid potentially
                // repeatedly dropping now and adding at the next time
                // (e.g., if the cell in front of this dummy cell is
                // slowly being written, while there is no new data to
                // add in the future)
                //
                // so do nothing here
            }
        }
    } else {
        // not actively defending

        auto amnt_to_write = curbufsize;
        if (whole_dummy_cell_at_end_outbuf_) {
            // don't need to write the dummy cell
            amnt_to_write = curbufsize - cell_size_;
            // amount to write could be zero if the only thing in the
            // out buf is a dummy cell
            CHECK_GE(amnt_to_write, 0) << amnt_to_write;
        }

        if (amnt_to_write > 0) {
            vlogself(2) << "tell socket to write " << amnt_to_write << " bytes";
            num_written = evbuffer_write_atmost(cell_outbuf_, fd_, amnt_to_write);
            vlogself(2) << "evbuffer_write_atmost() return: " << num_written;

            did_attempt_write = true;

            // if there's only a whole dummy cell left, then disable write
            // event, because the dummy cell will be dropped below
            _maybe_toggle_write_monitoring(
                ((num_written == amnt_to_write) && whole_dummy_cell_at_end_outbuf_)
                ? ForceToggleMode::FORCE_DISABLE
                : ForceToggleMode::NONE);
        } else {
            CHECK(whole_dummy_cell_at_end_outbuf_);
            _maybe_toggle_write_monitoring(ForceToggleMode::FORCE_DISABLE);
        }

        _maybe_drop_whole_dummy_cell_at_end_outbuf(__LINE__);
        CHECK(!whole_dummy_cell_at_end_outbuf_);
    }

    if (num_written > 0) {
        all_send_byte_count_ += num_written;
        _update_output_cell_progress(num_written);
    }

    if (did_attempt_write && num_written <= 0) {
        _handle_failed_socket_io("write", num_written, false);
    }

    vlogself(2) << "done";
}

/*
 * defense state must not be active. cuz if it's active we should be
 * writing every time the buflo timer fires, and not rely on the
 * socket being ready to write.
 */
void
BufloMuxChannelImplSpdy::_maybe_toggle_write_monitoring(ForceToggleMode forcemode)
{
    /* defense must NOT be active, otherwise must be forcing
     * disable */
    CHECK((defense_info_.state != DefenseState::ACTIVE)
          || (forcemode == ForceToggleMode::FORCE_DISABLE));

    auto rv = 0;

    if (forcemode == ForceToggleMode::FORCE_ENABLE) {
        goto enable;
    } else if (forcemode == ForceToggleMode::FORCE_DISABLE) {
        goto disable;
    } else {
        CHECK_EQ(forcemode, ForceToggleMode::NONE);
    }

    /* if we're not using cells, i.e., cell_size_ == 0, then check the
     * spdy_outbuf_
     */
    if (evbuffer_get_length(cell_size_ ? cell_outbuf_ : spdy_outbuf_) > 0) {
        goto enable;
    } else {
        goto disable;
    }

enable:
    vlogself(2) << "REALLY enable write event";
    rv = event_add(socket_write_ev_.get(), nullptr);
    CHECK_EQ(rv, 0);
    return;

disable:
    vlogself(2) << "REALLY disable write event";
    rv = event_del(socket_write_ev_.get());
    CHECK_EQ(rv, 0);
    return;
}

void
BufloMuxChannelImplSpdy::_handle_failed_socket_io(
    const char* io_op_str,
    const ssize_t rv,
    bool crash_if_EINPROGRESS)
{
    if (rv == 0) {
        _on_socket_eof();
    } else {
        DCHECK_EQ(rv, -1);
        if (errno == EAGAIN) {
            // can safely ingore
        } else if (errno == EINPROGRESS) {
            if (crash_if_EINPROGRESS) {
                logself(FATAL) << "getting EINPROGRESS after a " << io_op_str;
            }
        } else {
            logself(WARNING) << io_op_str << " got errno= " << errno
                             << " (" << strerror(errno) << ")";
            _on_socket_error();
        }
    }
}

void
BufloMuxChannelImplSpdy::_add_ONE_dummy_cell_to_outbuf()
{
    uint8_t type_n_flags = 0;
    SET_CELL_TYPE(type_n_flags, CellType::DUMMY);
    static const uint16_t len_field = 0;

    CHECK(!whole_dummy_cell_at_end_outbuf_);

    const auto did_set_important_flags =
        _maybe_set_cell_flags(&type_n_flags, "dummy");

    // add type and length
    auto rv = evbuffer_add(
        cell_outbuf_, (const uint8_t*)&type_n_flags, sizeof type_n_flags);
    CHECK_EQ(rv, 0);
    rv = evbuffer_add(
        cell_outbuf_, (const uint8_t*)&len_field, sizeof len_field);
    CHECK_EQ(rv, 0);

    rv = evbuffer_add_reference(
        cell_outbuf_, common::static_bytes->c_str(),
        cell_body_size_, nullptr, nullptr);
    CHECK_EQ(rv, 0);

    /* if the added dummy cell has important flags, we pretend it's
     * not a dummy cell
     */
    whole_dummy_cell_at_end_outbuf_ = !did_set_important_flags;
    output_cells_data_bytes_info_.push_back(0);
}

void
BufloMuxChannelImplSpdy::_ensure_a_whole_dummy_cell_at_end_outbuf()
{
    CHECK_EQ(defense_info_.state, DefenseState::ACTIVE);

    // if there's already one dummy cell at the end of outbuf then we
    // don't want to add more
    vlogself(2) << "whole_dummy_cell_at_end_outbuf_= "
                << whole_dummy_cell_at_end_outbuf_;
    if (whole_dummy_cell_at_end_outbuf_) {
        vlogself(2) << "no need to add dummy cell";
        return;
    }

    // if there's at least one cell in outbuf already, then we
    // shouldn't reach here... this might crash if there is a lot of
    // data in cell buf, but since we now want to send a flag and
    // there's no need data cell to be added, then we need to add a
    // dummy cell... unless we implement the better strategy of
    // setting flags in cells that are already in the cell_outbuf_. we
    // didn't do that because it would require book keeping to know
    // the boundaries of every cell in the cell outbuf, but now we
    // have that anyway with the front_cell_sent_progress_
    CHECK(evbuffer_get_length(cell_outbuf_) < cell_size_);

    _add_ONE_dummy_cell_to_outbuf();

    vlogself(2) << "added one dummy cell";
}

/*
 * returns true if did drop, false if nothing was done
 */
bool
BufloMuxChannelImplSpdy::_maybe_drop_whole_dummy_cell_at_end_outbuf(const int called_from_line,
                                                                    const bool do_count)
{
#define _WITH_CALLER_CHECK(cond) CHECK(cond) << "(called from line " << called_from_line << ") "

    vlogself(2) << "begin";

    bool did_drop = false;

    _WITH_CALLER_CHECK(cell_outbuf_);
    auto curbufsize = evbuffer_get_length(cell_outbuf_);

    if (whole_dummy_cell_at_end_outbuf_) {
        _WITH_CALLER_CHECK(curbufsize >= cell_size_) << "curbuf size= " << curbufsize;

        // this is optimization to drop whole dummy cell, because: if
        // at the next timer fired, we have USEFUL data to write, then
        // we will be able to write it instead of being blocked by the
        // dummy cell. (if on the other hand at the next timer fired,
        // we have no useful data to write, then removing the whole
        // dummy cell here does not help anything, since we'll have to
        // add it again at that time

        const auto amnt_to_keep = curbufsize - cell_size_;
        _WITH_CALLER_CHECK(amnt_to_keep >= 0); // just to be sure :D
        if (amnt_to_keep > 0) {
            // there is no api to remove at the end of the
            // buffer, so we replace it
            struct evbuffer* newbuf = evbuffer_new();
            _WITH_CALLER_CHECK(newbuf);

            const auto rv = evbuffer_remove_buffer(
                cell_outbuf_ /* src */,
                newbuf /* dst */,
                amnt_to_keep);
            _WITH_CALLER_CHECK(rv == amnt_to_keep);

            evbuffer_free(cell_outbuf_);
            cell_outbuf_ = newbuf;
            newbuf = nullptr;
            vlogself(2) << "keep " << amnt_to_keep << " of cell outbuf";
        } else {
            // should be rare, but we can just drain the
            // whole buf
            vlogself(2) << "drain cell outbuf";
            const auto rv = evbuffer_drain(cell_outbuf_, curbufsize);
            _WITH_CALLER_CHECK(rv == 0);
        }

        curbufsize = evbuffer_get_length(cell_outbuf_);

        _WITH_CALLER_CHECK(curbufsize == amnt_to_keep)
            << "amnt_to_keep= " << amnt_to_keep << " curbufsize= " << curbufsize;

        CHECK(whole_dummy_cell_at_end_outbuf_);

        whole_dummy_cell_at_end_outbuf_ = false;
        _WITH_CALLER_CHECK(!output_cells_data_bytes_info_.empty());
        _WITH_CALLER_CHECK(output_cells_data_bytes_info_.back() == 0);
        output_cells_data_bytes_info_.pop_back();

        did_drop = true;

        if (do_count) {
            ++num_dummy_cells_avoided_;
            vlogself(1) << "woot! avoided a dummy cell, by " << called_from_line;
        }
    }

#undef _WITH_CALLER_CHECK

    vlogself(2) << "done";

    return did_drop;
}

void
BufloMuxChannelImplSpdy::_read_cells()
{
    vlogself(2) << "begin";
    CHECK(peer_cell_size_ > 0);

    // todo: maybe limit how much time we spend in here, i.e., yield
    // after X ms, so that we can send cells on time and not miss the
    // defense timer

    auto keep_consuming = true;

    // only drain buffer after we read full msg, to reduce memory
    // operations

    /* for simplicity, right now we will wait for WHOLE CELL to be
     * available before processing
     *
     * a possible optimization is to do "dropread" of padding/dummy
     * cells
     */

    do {
        const auto num_avail_bytes = evbuffer_get_length(cell_inbuf_);
        vlogself(2) << "num_avail_bytes= " << num_avail_bytes;

        // loop to process all complete msgs
        switch (cell_read_info_.state_) {
        case ReadState::READ_HEADER:
            vlogself(2) << "trying to read msg type and length";
            if (num_avail_bytes >= CELL_HEADER_SIZE) {
                // we're using a libevent version without
                // copyout[_from]()
                auto buf = evbuffer_pullup(cell_inbuf_, CELL_HEADER_SIZE);
                CHECK_NOTNULL(buf);

                uint8_t type_n_flags = 0;
                memcpy((uint8_t*)&type_n_flags,
                       buf, CELL_TYPE_AND_FLAGS_FIELD_SIZE);
                memcpy((uint8_t*)&cell_read_info_.payload_len_,
                       buf + CELL_TYPE_AND_FLAGS_FIELD_SIZE,
                       CELL_PAYLOAD_LEN_FIELD_SIZE);

                cell_read_info_.cell_type_ = GET_CELL_TYPE(type_n_flags);
                cell_read_info_.cell_flags_ = GET_CELL_FLAGS(type_n_flags);

                // convert to host byte order
                cell_read_info_.payload_len_ =
                    ntohs(cell_read_info_.payload_len_);

                // update state
                cell_read_info_.state_ = ReadState::READ_BODY;
                vlogself(2) << "got type= "
                            << unsigned(cell_read_info_.cell_type_)
                            << ", payload len= "
                            << cell_read_info_.payload_len_;

            } else {
                vlogself(2) << "not enough bytes yet";
                keep_consuming = false; // to break out of loop
            }
            break;
        case ReadState::READ_BODY:
            if (num_avail_bytes >= peer_cell_size_) {

                // this is responsible for removing the cell from the
                // cell inbuf
                _handle_input_cell();

                cell_read_info_.reset();
            } else {
                vlogself(2) << "not enough bytes yet";
                keep_consuming = false; // to break out of loop
            }
            break;
        default:
            CHECK(false); // not reached
        }
    } while (keep_consuming);

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::_handle_input_cell()
{
    /*
     * the whole cell, including header, is available at the front of
     * the cell_inbuf_, but it might not be contiguous---call
     * evbuffer_pullup() if you need
     *
     * responsible for removing the cell from the input buf
     */

    const auto payload_len = cell_read_info_.payload_len_;
    CHECK_GE(payload_len, 0);
    CHECK_LE(payload_len, peer_cell_size_);

    vlogself(2) << "begin";

    /* whether peer tells us it's done defending sending, i.e., our
     * receive */
    bool done_defending_recv = false;

    auto ptr = evbuffer_pullup(cell_inbuf_, CELL_HEADER_SIZE + payload_len);

    // handle any flags
    const auto cell_flags = (cell_read_info_.cell_flags_);
    if (cell_flags) {
        const bitset<CELL_FLAGS_WIDTH> flags_bs(cell_flags);
        vlogself(1) << "received cell flags: " << flags_bs;

        const auto start_defense = flags_bs.test(CELL_FLAGS_START_DEFENSE_POSITION);
        const auto stop_defense = flags_bs.test(CELL_FLAGS_STOP_DEFENSE_POSITION);
        const auto defense_auto_stopped = flags_bs.test(CELL_FLAGS_DEFENSE_AUTO_STOPPED_POSITION);
        const auto defensive = flags_bs.test(CELL_FLAGS_DEFENSIVE_POSITION);
        done_defending_recv = flags_bs.test(CELL_FLAGS_DEFENSE_DONE_POSITION);

        if (defense_auto_stopped) {
            // only ssp auto-stops and notifies csp about it
            CHECK(is_client_side_);
            logself(WARNING) << "SSP has auto-stopped its defense";

            // if we're still active and not stopping soon, ask ssp to
            // start again
            if ((defense_info_.state == DefenseState::ACTIVE)
                && (!defense_info_.stop_requested))
            {
                logself(INFO) << "ask SSP to start again";
                CHECK(!defense_info_.need_start_flag_in_next_cell);
                defense_info_.need_start_flag_in_next_cell = true;
            }
        }

        // cannot both start and stop
        CHECK(! (start_defense && stop_defense));

        if (start_defense) {
            CHECK(!is_client_side_);
            logself(INFO) << "start defending as requested by csp";
            start_defense_session();
        }

        if (stop_defense) {
            CHECK(!is_client_side_);
            logself(INFO) << "schedule to stop defense requested by csp";
            stop_defense_session();
        }

        if (defensive) {
            ++defense_info_.num_cells_recv;
        }
    }

    // handle data
    const auto cell_type = (cell_read_info_.cell_type_);
    const char* cell_type_str = nullptr;

    vlogself(2) << "type: " << unsigned(cell_type) << " payload_len: " << payload_len;

    switch (cell_type) {

    case CellType::DATA: {
        auto rv = evbuffer_add(spdy_inbuf_, ptr+CELL_HEADER_SIZE, payload_len);
        CHECK_EQ(rv, 0);

        all_users_data_recv_byte_count_ += payload_len;

        _pump_spdy_recv();
        cell_type_str = "data";
        break;
    }

    case CellType::DUMMY: {
        // do nothing
        ++dummy_recv_cell_count_;
        cell_type_str = "dummy";
        break;
    }

    case CellType::CONTROL: {
        logself(FATAL) << "to do";
        break;
    }

    default:
        logself(FATAL) << "not reached";
        break;
    }

    if (done_defending_recv) {
        CHECK(is_client_side_);

        defense_info_.done_defending_recv = true;

        logself(INFO) << "done defending recv (notified via a "
                      << cell_type_str << " cell)";

        _check_notify_a_defense_session_done(__LINE__);
    }

    // now drain the whole cell
    auto rv = evbuffer_drain(cell_inbuf_, peer_cell_size_);
    CHECK_EQ(rv, 0);

    vlogself(2) << "done";

    return;
}

void
BufloMuxChannelImplSpdy::_on_socket_eof()
{
    DestructorGuard dg(this);
    ch_status_cb_(this, ChannelStatus::CLOSED);
}

void
BufloMuxChannelImplSpdy::_on_socket_error()
{
    DestructorGuard dg(this);
    ch_status_cb_(this, ChannelStatus::CLOSED);
}

void
BufloMuxChannelImplSpdy::_check_notify_a_defense_session_done(const int called_from_line)
{
    vlogself(2) << "begin (called from line " << called_from_line << "): "
                << (defense_info_.state == DefenseState::NONE) << " "
                << defense_info_.done_defending_recv;

    if ((defense_info_.state == DefenseState::NONE)
        && defense_info_.done_defending_recv)
    {
        DestructorGuard dg(this);
        logself(INFO) << "defense session (both directions) done; "
                      << "defensive cells sent/attempted= "
                      << defense_info_.saved_num_write_attempts
                      << " received= "
                      << defense_info_.num_cells_recv;
        defense_info_.done_defending_recv = false;
        defense_info_.num_cells_recv = 0;
        defense_info_.saved_num_write_attempts = 0;
        ch_status_cb_(this, ChannelStatus::A_DEFENSE_SESSION_DONE);
    }

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::_on_socket_readcb(int fd, short what)
{
    vlogself(2) << "begin, what= " << unsigned(what);

    DestructorGuard dg(this);

    if (what & (EV_READ | EV_TIMEOUT)) {
        if (need_to_read_peer_info_) {
            vlogself(2) << "read peer info";
            const auto still_need =
                PEER_INFO_NUM_BYTES - evbuffer_get_length(peer_info_inbuf_);
            CHECK(still_need > 0);
            const auto rv = evbuffer_read(peer_info_inbuf_, fd_, still_need);
            if (rv > 0) {
                CHECK(evbuffer_get_length(peer_info_inbuf_) <= PEER_INFO_NUM_BYTES);
                if (evbuffer_get_length(peer_info_inbuf_) == PEER_INFO_NUM_BYTES) {
                    _read_peer_info();
                }
            } else {
                _handle_failed_socket_io("readPeerInfo", rv, true);
            }
        } else {
            // let buffer decide how much to read. if peer is not
            // sending cells, then we read directly into spdy buf
            const auto rv = evbuffer_read(
                peer_cell_size_ ? cell_inbuf_ : spdy_inbuf_, fd_, -1);
            vlogself(2) << "evbuffer_read() returns: " << rv;
            if (rv > 0) {
                // there's new data
                all_recv_byte_count_ += rv;

                if (peer_cell_size_) {
                    _read_cells();
                } else {
                    all_users_data_recv_byte_count_ += rv;

                    _pump_spdy_recv();
                }
            } else {
                _handle_failed_socket_io("read", rv, true);
            }
        }
    } else {
        CHECK(0) << "invalid events: " << unsigned(what);
    }

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::_on_socket_writecb(int fd, short what)
{
    // if defense activated, we should write on our schedule, and not
    // depend on socket events. if activating defense after the write
    // event has been added, then can tell libevent to remove the
    // event so we don't reach here
    CHECK_NE(defense_info_.state, DefenseState::ACTIVE);

    vlogself(2) << "begin";

    DestructorGuard dg(this);

    if (what & EV_WRITE) {
        if (my_peer_info_outbuf_) {
            vlogself(2) << "write my peer info buf";
            _write_my_peer_info_outbuf();
            CHECK(!my_peer_info_outbuf_);
            _maybe_toggle_write_monitoring(ForceToggleMode::FORCE_DISABLE);
        } else {

            if (cell_size_) {
                if (defense_info_.state == DefenseState::PENDING_NEXT_SOCKET_SEND) {
                    // for now, to keep logic simple, we insist that the
                    // cell_outbuf_ has EXACTLY ONE DATA cell; but we can only
                    // check that cell_outbuf_ has one cell
                    CHECK_EQ(evbuffer_get_length(cell_outbuf_), cell_size_);

                    vlogself(2) << "automatically starting the defense";
                    // start_defense_session() will set to ACTIVE
                    defense_info_.state = DefenseState::NONE;
                    start_defense_session();
                    // we have only started the timer. we will fall through to
                    // do the first write here
                }
                _send_cell_outbuf();

            } else {
                /* we are operating as straigt-up regular proxy, i.e.,
                 * user's traffic is NOT packed into fixed size cells,
                 * and no buflo defense */
                const auto curbufsize = evbuffer_get_length(spdy_outbuf_);
                vlogself(2) << "curbufsize= " << curbufsize;
                CHECK(curbufsize > 0) << "curbufsize= " << curbufsize;

                const auto num_written = evbuffer_write(spdy_outbuf_, fd_);
                vlogself(2) << "evbuffer_write() return: " << num_written;

                if (num_written > 0) {
                    all_send_byte_count_ += num_written;
                    all_users_data_send_byte_count_ += num_written;
                    if (num_written == curbufsize) {
                        // we have fully emptied the output buffer, so
                        // we can disable write event
                        _maybe_toggle_write_monitoring(ForceToggleMode::FORCE_DISABLE);
                    }
                } else {
                    _handle_failed_socket_io("write", num_written, true);
                }

            }
        }
    } else {
        CHECK(0) << "invalid events: " << unsigned(what);
    }

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::_read_peer_info()
{
    CHECK(need_to_read_peer_info_);
    CHECK_EQ(evbuffer_get_length(peer_info_inbuf_), PEER_INFO_NUM_BYTES);

    static_assert(PEER_INFO_NUM_BYTES == (1 + 2 + 4 + 2 + 2),
                  "unexpected PEER_INFO_NUM_BYTES");

    uint8_t peer_version = 0;

    auto rv = evbuffer_copyout(peer_info_inbuf_, (uint8_t*)&peer_version, 1);
    CHECK_EQ(rv, 1);
    rv = evbuffer_drain(peer_info_inbuf_, 1);
    CHECK_EQ(rv, 0);

    rv = evbuffer_copyout(peer_info_inbuf_, (uint8_t*)&peer_cell_size_, 2);
    CHECK_EQ(rv, 2);
    rv = evbuffer_drain(peer_info_inbuf_, 2);
    CHECK_EQ(rv, 0);

    peer_cell_size_ = ntohs(peer_cell_size_);

    peer_cell_body_size_ = peer_cell_size_ - CELL_HEADER_SIZE;

    peeraddr_ = 0;
    rv = evbuffer_copyout(peer_info_inbuf_, (uint8_t*)&peeraddr_, 4);
    CHECK_EQ(rv, 4);
    rv = evbuffer_drain(peer_info_inbuf_, 4);
    CHECK_EQ(rv, 0);

    uint16_t requested_L = 0;
    rv = evbuffer_copyout(peer_info_inbuf_, (uint8_t*)&requested_L, 2);
    CHECK_EQ(rv, 2);
    rv = evbuffer_drain(peer_info_inbuf_, 2);
    CHECK_EQ(rv, 0);

    requested_L = ntohs(requested_L);

    uint16_t requested_pkt_intvl = 0;
    rv = evbuffer_copyout(peer_info_inbuf_, (uint8_t*)&requested_pkt_intvl, 2);
    CHECK_EQ(rv, 2);
    rv = evbuffer_drain(peer_info_inbuf_, 2);
    CHECK_EQ(rv, 0);

    requested_pkt_intvl = ntohs(requested_pkt_intvl);

    logself(INFO) << "peer IP is " << peer_ip()
                  << " version= " << unsigned(peer_version)
                  << " using cell size= " << peer_cell_size_;

    if (!is_client_side_) {
        // server-side can now enable the write event... we used to
        // just write right here, but sometimes socket did not accept
        // our write
        _maybe_toggle_write_monitoring(ForceToggleMode::FORCE_ENABLE);
    }

    if (peer_version != s_version) {
        logself(WARNING)
            << "version mismatch: mine is " << unsigned(s_version)
            << " peer's is " << unsigned(peer_version);
        if (is_client_side_) {
            logself(FATAL) << "use correct client version";
        } else {
            logself(WARNING) << "tearing down connection with peer";
            _close_socket_and_events();
            ch_status_cb_(this, ChannelStatus::CLOSED);
            return;
        }
    }

    if (requested_L) {
        if (is_client_side_) {
            logself(FATAL) << "SSP unexpectedly requests L= " << requested_L;
        } else {
            logself(INFO) << "CSP requests L= " << requested_L;
            tamaraw_L_ = requested_L;
            CHECK(_check_L(tamaraw_L_)) << "bad requested L= " << tamaraw_L_;
        }
    }

    if (requested_pkt_intvl) {
        if (is_client_side_) {
            logself(FATAL) << "SSP unexpectedly requests pkt interval= " << requested_pkt_intvl;
        } else {
            logself(INFO) << "CSP requests pkt interval= " << requested_pkt_intvl;
            // CSP wants us to use this pkt interval, and we oblige,
            // after validating it
            CHECK(_check_pkt_intvl(requested_pkt_intvl))
                << "bad requested pkt interval= " << requested_pkt_intvl;
            tamaraw_pkt_intvl_ms_ = requested_pkt_intvl;
        }
    }

    need_to_read_peer_info_ = false;

    DestructorGuard dg(this);
    ch_status_cb_(this, ChannelStatus::READY);
}

std::string
BufloMuxChannelImplSpdy::peer_ip() const
{
    struct in_addr ip_addr;
    ip_addr.s_addr = peeraddr_;
    string s = inet_ntoa(ip_addr);
    return s;
}

void
BufloMuxChannelImplSpdy::_fill_my_peer_info_outbuf()
{
    CHECK_NOTNULL(my_peer_info_outbuf_);
    CHECK_EQ(evbuffer_get_length(my_peer_info_outbuf_), 0);

    auto rv = evbuffer_add(my_peer_info_outbuf_, (uint8_t*)&s_version, 1);
    CHECK_EQ(rv, 0);

    const uint16_t cs = htons(cell_size_);
    rv = evbuffer_add(my_peer_info_outbuf_, (uint8_t*)&cs, 2);
    CHECK_EQ(rv, 0);

    const in_addr_t addr = htonl(myaddr_);
    rv = evbuffer_add(my_peer_info_outbuf_, (uint8_t*)&addr, 4);
    CHECK_EQ(rv, 0);

    // only csp sends L (tell ssp to use the same L as csp). (ssp
    // sends 0.)
    const uint16_t L = is_client_side_ ? htons(tamaraw_L_) : 0;
    rv = evbuffer_add(my_peer_info_outbuf_, (uint8_t*)&L, 2);
    CHECK_EQ(rv, 0);

    // only csp sends pkt interval. (ssp sends 0.)
    const uint16_t pkt_intvl = is_client_side_ ? htons(peer_tamaraw_pkt_intvl_ms_) : 0;
    rv = evbuffer_add(my_peer_info_outbuf_, (uint8_t*)&pkt_intvl, 2);
    CHECK_EQ(rv, 0);

    CHECK_EQ(evbuffer_get_length(my_peer_info_outbuf_), PEER_INFO_NUM_BYTES);
}

void
BufloMuxChannelImplSpdy::_write_my_peer_info_outbuf()
{
    auto buflen = evbuffer_get_length(my_peer_info_outbuf_);
    CHECK_EQ(buflen, PEER_INFO_NUM_BYTES) << "unexpected buf len: " << buflen;
    const auto rv = evbuffer_write(my_peer_info_outbuf_, fd_);

    // we only write a small amount, so we should be able empty the
    // buffer
    CHECK_EQ(rv, PEER_INFO_NUM_BYTES) << "write rv: " << rv;

    buflen = evbuffer_get_length(my_peer_info_outbuf_);
    CHECK_EQ(buflen, 0) << "unexpected buf len: " << buflen;

    evbuffer_free(my_peer_info_outbuf_);
    my_peer_info_outbuf_ = nullptr;
}

void
BufloMuxChannelImplSpdy::s_spdylay_on_stream_close_cb(spdylay_session *session,
                                                      int32_t stream_id,
                                                      spdylay_status_code status_code,
                                                      void *user_data)
{
    auto *ch = (BufloMuxChannelImplSpdy*)user_data;
    ch->_on_spdylay_on_stream_close_cb(session, stream_id, status_code);
}

void
BufloMuxChannelImplSpdy::_on_spdylay_on_stream_close_cb(spdylay_session *session,
                                                        int32_t stream_id,
                                                        spdylay_status_code status_code)
{
    DestructorGuard dg(this);

    vlogself(2) << "begin, sid= " << stream_id << " code= " << status_code;
    auto *observer = (BufloMuxChannelStreamObserver*)
                     spdylay_session_get_stream_user_data(session, stream_id);
    if (observer) {
        observer->onStreamClosed(this, stream_id);
    }

    // erase will do nothing if stream_id is not in map
    stream_states_.erase(stream_id);
    vlogself(2) << "done";
}

ssize_t
BufloMuxChannelImplSpdy::_on_spdylay_send_cb(
    spdylay_session *session,
    const uint8_t *data,
    size_t length,
    int flags)
{
    /*
     * spdylay tells us it wants to write the bytes. we add the bytes
     * to the spdy output buf for writing later (either on buflo
     * schedule or when socket is writable, etc.)
     *
     */

    vlogself(2) << "begin, length= " << length;
    CHECK_EQ(session, spdysess_);

    const auto rv = evbuffer_add(spdy_outbuf_, data, length);
    CHECK_EQ(rv, 0);
    // we should always be able to add all the bytes to the buf
    auto retval = length;

    // if (rv == 0) {
    //     retval = length;
    // }
    // else if (rv == -1) {
    //     logself(WARNING) << "outbuf didn't accept our add";
    //     retval = SPDYLAY_ERR_CALLBACK_FAILURE;
    // } else {
    //     logself(FATAL) << "invalid rv= " << rv;
    // }
 
    vlogself(2) << "done, returning " << retval;
    return retval;
}

ssize_t
BufloMuxChannelImplSpdy::_on_spdylay_recv_cb(
    spdylay_session *session,
    uint8_t *data,
    size_t length,
    int flags)
{
    vlogself(2) << "begin, length= " << length;
    CHECK_EQ(session, spdysess_);

    const auto rv = evbuffer_remove(spdy_inbuf_, data, length);
    CHECK_GE(rv, 0);
    // we should never get error from evbuffer
    auto retval = (rv > 0) ? rv : SPDYLAY_ERR_WOULDBLOCK;

    // else if (rv == -1) {
    //     logself(WARNING) << "inbuf didn't accept our remove";
    //     retval = SPDYLAY_ERR_CALLBACK_FAILURE;
    // } else {
    //     logself(FATAL) << "invalid rv= " << rv;
    // }

    vlogself(2) << "done, returning " << retval;
    return retval;
}

void
BufloMuxChannelImplSpdy::_on_spdylay_before_ctrl_send_cb(
    spdylay_session *session,
    spdylay_frame_type type,
    spdylay_frame *frame)
{
    DestructorGuard dg(this);

    vlogself(2) << "begin";

    CHECK_EQ(session, spdysess_);

    if (type == SPDYLAY_SYN_STREAM) {
        // only client side should send syn stream (i.e., no
        // server-push allowed)
        CHECK(is_client_side_);

        /*
         * this is the earliest the client-side has the stream id
         */

        const int32_t sid = frame->syn_stream.stream_id;
        auto *observer = (BufloMuxChannelStreamObserver*)
            spdylay_session_get_stream_user_data(session, sid);
        CHECK_NOTNULL(observer);

        _init_stream_state(sid);

        _init_stream_data_provider(sid);

        vlogself(2) << "notify that stream id is assigned";
        observer->onStreamIdAssigned(this, sid);
    }

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::_init_stream_state(const int& sid)
{
    std::unique_ptr<StreamState> sstate(new StreamState());
    const auto ret = stream_states_.insert(
        make_pair(sid, std::move(sstate)));
    CHECK(ret.second); // insist it was newly inserted
}

void
BufloMuxChannelImplSpdy::_init_stream_data_provider(const int& sid)
{
    spdylay_data_provider data_provider;
    bzero(&data_provider, sizeof data_provider);
    data_provider.read_callback = s_spdylay_data_read_cb;

    // don't use flag_fin because it would mean this would be the last
    // frame sent on this stream
    const auto rv = spdylay_submit_data(spdysess_, sid, SPDYLAY_DATA_FLAG_FIN, &data_provider);
    CHECK_EQ(rv, 0);
}

void
BufloMuxChannelImplSpdy::_on_spdylay_on_ctrl_recv_cb(spdylay_session *session,
                                                     spdylay_frame_type type,
                                                     spdylay_frame *frame)
{
    DestructorGuard dg(this);

    vlogself(2) << "begin";

    switch(type) {
    case SPDYLAY_SYN_STREAM: {
        /*
         * this is the earliest the server-side knows about the
         * stream/sid
         */

        const auto sid = frame->syn_stream.stream_id;
        // only the server side should receive this
        CHECK(!is_client_side_);

        string host_hdr;

        auto nv = frame->syn_stream.nv;
        for (int i = 0; nv[i]; i += 2) {
            if (!strcmp(nv[i], ":host")) {
                host_hdr = nv[i+1];
                break;
            }
        }

        vlogself(2) << "host hdr= [" << host_hdr << "]";

        auto const colon_pos = host_hdr.find(':');
        CHECK_GT(colon_pos, 0);

        string host_str = host_hdr.substr(0, colon_pos);
        string port_str = host_hdr.substr(colon_pos + 1);
        const uint16_t port = boost::lexical_cast<uint16_t>(port_str);

        vlogself(2) << "host= [" << host_str << "] port= " << port;

        _init_stream_state(sid);

        DestructorGuard dg(this);
        st_connect_req_cb_(this, sid, host_str.c_str(), port);

        break;
    }

    case SPDYLAY_SYN_REPLY: {
        /*
         * this must be the csp receiving the reply from ssp
         */

        const auto sid = frame->syn_stream.stream_id;
        CHECK(is_client_side_);

        string ok_value;
        // make sure to use frame->syn_XYZ of the right type
        auto nv = frame->syn_reply.nv;
        for (int i = 0; nv[i]; i += 2) {
            if (!strcmp(nv[i], "ok")) {
                ok_value = nv[i+1];
                break;
            }
        }

        // if the ssp couldn't connect to target, we expect that it
        // would have just reset the stream
        CHECK_EQ(ok_value.compare("true"), 0);

        auto *observer = (BufloMuxChannelStreamObserver*)
                         spdylay_session_get_stream_user_data(session, sid);
        CHECK_NOTNULL(observer);
        observer->onStreamCreateResult(this, true, 0, 0);
        break;

    }

    case SPDYLAY_RST_STREAM: {
        const auto sid = frame->syn_stream.stream_id;
        vlogself(2) << "stream: " << sid << " being reset by peer";
        // we don't notify observer because we will reach
        // _on_spdylay_on_stream_close_cb() and let it notify user if
        // appropriate
        break;
    }

    default:
        break;
    }

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::s_spdylay_on_ctrl_recv_cb(spdylay_session *session,
                                                   spdylay_frame_type type,
                                                   spdylay_frame *frame,
                                                   void *user_data)
{
    auto ch = (BufloMuxChannelImplSpdy*)user_data;
    ch->_on_spdylay_on_ctrl_recv_cb(session, type, frame);
}

void
BufloMuxChannelImplSpdy::_on_spdylay_on_request_recv_cb(const int sid)
{
    logself(FATAL) << "should not reached";
}

void
BufloMuxChannelImplSpdy::s_spdylay_on_request_recv_cb(spdylay_session *session,
                                                      int32_t stream_id,
                                                      void *user_data)
{
    auto *ch = (BufloMuxChannelImplSpdy*)user_data;
    ch->_on_spdylay_on_request_recv_cb(stream_id);
}

void
BufloMuxChannelImplSpdy::_setup_spdylay_session()
{
    spdylay_session_callbacks callbacks;
    bzero(&callbacks, sizeof callbacks);
    callbacks.send_callback = s_spdylay_send_cb;
    callbacks.recv_callback = s_spdylay_recv_cb;
    callbacks.on_ctrl_recv_callback = s_spdylay_on_ctrl_recv_cb;
    callbacks.on_data_chunk_recv_callback = s_spdylay_on_data_chunk_recv_cb;
    callbacks.on_data_recv_callback = s_spdylay_on_data_recv_cb;
    callbacks.on_stream_close_callback = s_spdylay_on_stream_close_cb;
    callbacks.before_ctrl_send_callback = s_spdylay_before_ctrl_send_cb;
    // callbacks.on_ctrl_not_send_callback = s_spdylay_on_ctrl_not_send_cb;

    spdylay_session *session = nullptr;

    // version 3 just adds flow control, compared to version 2

    // we use version 2 to avoid issue #2
    static const auto proto_ver = SPDYLAY_PROTO_SPDY2;

    auto rv = 0;
    if (is_client_side_) {
        rv = spdylay_session_client_new(&session, proto_ver, &callbacks, this);
    } else {
        rv = spdylay_session_server_new(&session, proto_ver, &callbacks, this);
    }
    CHECK_EQ(rv, 0);

#if 0
    spdylay_settings_entry entry[2];
    entry[0].settings_id = SPDYLAY_SETTINGS_MAX_CONCURRENT_STREAMS;
    entry[0].value = 8; // XXX
    entry[0].flags = SPDYLAY_ID_FLAG_SETTINGS_NONE;

    entry[1].settings_id = SPDYLAY_SETTINGS_INITIAL_WINDOW_SIZE;
    entry[1].value = 4096; // XXX
    entry[1].flags = SPDYLAY_ID_FLAG_SETTINGS_NONE;

    r = spdylay_submit_settings(
        session_, SPDYLAY_FLAG_SETTINGS_NONE,
        entry, sizeof(entry)/sizeof(spdylay_settings_entry));
    CHECK (0 == r);
#endif

    spdysess_ = session;
    session = nullptr;

    return;
}

void
BufloMuxChannelImplSpdy::s_spdylay_before_ctrl_send_cb(
    spdylay_session *session,
    spdylay_frame_type type,
    spdylay_frame *frame,
    void *user_data)
{
    auto *conn = (BufloMuxChannelImplSpdy*)(user_data);
    conn->_on_spdylay_before_ctrl_send_cb(session, type, frame);
}

void
BufloMuxChannelImplSpdy::s_socket_readcb(int fd, short what, void* arg)
{
    BufloMuxChannelImplSpdy* ch = (BufloMuxChannelImplSpdy*)arg;
    ch->_on_socket_readcb(fd, what);
}

void
BufloMuxChannelImplSpdy::s_socket_writecb(int fd, short what, void* arg)
{
    BufloMuxChannelImplSpdy* ch = (BufloMuxChannelImplSpdy*)arg;
    ch->_on_socket_writecb(fd, what);
}

ssize_t
BufloMuxChannelImplSpdy::s_spdylay_send_cb(spdylay_session *session,
                                           const uint8_t *data,
                                           size_t length,
                                           int flags,
                                           void *user_data)
{
    auto *ch = (BufloMuxChannelImplSpdy*)(user_data);
    return ch->_on_spdylay_send_cb(session, data, length, flags);;
}

ssize_t
BufloMuxChannelImplSpdy::s_spdylay_recv_cb(spdylay_session *session,
                                           uint8_t *data,
                                           size_t length,
                                           int flags,
                                           void *user_data)
{
    auto *ch = (BufloMuxChannelImplSpdy*)(user_data);
    return ch->_on_spdylay_recv_cb(session, data, length, flags);;
}

void
BufloMuxChannelImplSpdy::s_spdylay_on_data_chunk_recv_cb(
    spdylay_session *session,
    uint8_t flags, int32_t stream_id,
    const uint8_t *data, size_t len,
    void *user_data)
{
    auto *ch = (BufloMuxChannelImplSpdy*)user_data;
    ch->_on_spdylay_on_data_chunk_recv_cb(session, flags, stream_id,
                                          data, len);
}

void
BufloMuxChannelImplSpdy::_on_spdylay_on_data_chunk_recv_cb(
    spdylay_session *session,
    uint8_t flags,
    int32_t stream_id,
    const uint8_t *data,
    size_t len)
{
    DestructorGuard dg(this);

    vlogself(2) << "got chunk of " << len << " bytes from inner";

    MAYBE_GET_STREAMSTATE(stream_id, true, );

    streamstate->total_recv_from_inner_ += len;
    vlogself(2) << "   so far " << streamstate->total_recv_from_inner_;

    const auto rv = evbuffer_add(streamstate->outward_buf_, data, len);
    CHECK_NE(rv, -1);

    auto *observer = (BufloMuxChannelStreamObserver*)
                     spdylay_session_get_stream_user_data(session, stream_id);
    if (observer) {
        observer->onStreamNewDataAvailable(this, stream_id);
    } else {
        logself(WARNING) << "no observer for sid " << stream_id
                         << " to notify of new available data";
    }
}

void
BufloMuxChannelImplSpdy::s_spdylay_on_data_recv_cb(spdylay_session *session,
                                                      uint8_t flags,
                                                      int32_t stream_id,
                                                      int32_t length,
                                                      void *user_data)
{
    auto *ch = (BufloMuxChannelImplSpdy*)user_data;
    ch->_on_spdylay_on_data_recv_cb(session, flags, stream_id, length);
}

void
BufloMuxChannelImplSpdy::_on_spdylay_on_data_recv_cb(spdylay_session *session,
                                                     uint8_t flags,
                                                     int32_t stream_id,
                                                     int32_t length)
{
    DestructorGuard dg(this);

    if ((flags & SPDYLAY_DATA_FLAG_FIN) != 0) {
        MAYBE_GET_STREAMSTATE(stream_id, true, );

        CHECK(!streamstate->inner_recv_eof_);
        streamstate->inner_recv_eof_ = true;

        auto *observer = (BufloMuxChannelStreamObserver*)
                         spdylay_session_get_stream_user_data(session, stream_id);
        if (observer) {
            observer->onStreamRecvEOF(this, stream_id);
        } else {
            logself(WARNING) << "no observer for sid " << stream_id
                             << " to notify of eof";
        }
    }
}

ssize_t
BufloMuxChannelImplSpdy::s_spdylay_data_read_cb(spdylay_session *session,
                       int32_t stream_id,
                       uint8_t *buf, size_t length,
                       int *eof,
                       spdylay_data_source *source,
                       void *user_data)
{
    auto *ch = (BufloMuxChannelImplSpdy*)user_data;
    return ch->_on_spdylay_data_read_cb(
        session, stream_id, buf, length, eof, source);
}

ssize_t
BufloMuxChannelImplSpdy::_on_spdylay_data_read_cb(spdylay_session *session,
                                 int32_t stream_id,
                                 uint8_t *buf, size_t length,
                                 int *eof,
                                 spdylay_data_source *source)
{

    vlogself(2) << "begin, sid: " << stream_id;

    ssize_t retval = SPDYLAY_ERR_CALLBACK_FAILURE;

    MAYBE_GET_STREAMSTATE(stream_id, true, SPDYLAY_ERR_TEMPORAL_CALLBACK_FAILURE);

    const auto rv = evbuffer_remove(streamstate->inward_buf_, buf, length);
    CHECK_NE(rv, -1);

    if (rv > 0) {
        // able to read some bytes
        retval = rv;
    } else {
        CHECK_EQ(rv, 0);

        if (!streamstate->inward_has_seen_eof_) {
            vlogself(2) << "nothing to read, so we defer";
            streamstate->inward_deferred_ = true;
            retval = SPDYLAY_ERR_DEFERRED;
        } else {
            vlogself(2) << "nothing to read, has reached eof";
            *eof = 1;
            retval = 0;
        }
    }

    vlogself(2) << "done, returning: " << retval;
    return retval;
}

void
BufloMuxChannelImplSpdy::_close_socket_and_events()
{
    // delete these events BEFORE closing the fd; this seems to fix
    // issue #6
    socket_read_ev_.reset();
    socket_write_ev_.reset();

    if (fd_) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool
BufloMuxChannelImplSpdy::_check_L(const uint16_t& L) const
{
    if (!(   (tamaraw_L_ == 0)
          || (tamaraw_L_ == 50)
          || (tamaraw_L_ == 100)
          || (tamaraw_L_ == 150)
          || (tamaraw_L_ == 200)
          || (tamaraw_L_ == 250)
          || (tamaraw_L_ == 300)))
    {
        logself(ERROR) << "currently L should be 50, 100, 150, 200, 250, or 300";
        return false;
    }
    else {
        return true;
    }
}

bool
BufloMuxChannelImplSpdy::_check_pkt_intvl(const uint16_t& intvl) const
{
    if (!(   (intvl == 0)
          || (intvl == 5)
          || (intvl == 20)
          || (intvl == 50)
          || (intvl == 75)
          || (intvl == 100)
          || (intvl == 125)))
    {
        logself(ERROR) << "unsupported pkt interval: " << intvl;
        return false;
    }
    else {
        return true;
    }
}

BufloMuxChannelImplSpdy::~BufloMuxChannelImplSpdy()
{
    vlogself(2) << "begin destructing";
    _close_socket_and_events();

#define FREE_EVBUF(buf)                         \
    do {                                        \
        if (buf) {                              \
            evbuffer_free(buf);                 \
            buf = nullptr;                      \
        }                                       \
    } while (0)

    FREE_EVBUF(spdy_inbuf_);
    FREE_EVBUF(spdy_outbuf_);
    FREE_EVBUF(peer_info_inbuf_);
    FREE_EVBUF(my_peer_info_outbuf_);
    FREE_EVBUF(cell_inbuf_);
    FREE_EVBUF(cell_outbuf_);

    if (spdysess_) {
        spdylay_session_del(spdysess_);
        spdysess_ = nullptr;
    }

    vlogself(2) << "done destructing";
}

} // end namespace buflo
} // end namespace myio


static void
_self_test_bit_manipulation()
{
    VLOG(2) << "begin";

    static bool tested = false;
    if (tested) {
        VLOG(2) << "already tested, skipping";
        return;
    }

    using std::bitset;

#define TEST_GET(t_n_f_val, exp_t_val, exp_f_val)                       \
    do {                                                                \
        const uint8_t type_n_flags = t_n_f_val;                         \
        const uint8_t type = GET_CELL_TYPE(type_n_flags);               \
        const uint8_t flags = GET_CELL_FLAGS(type_n_flags);             \
        VLOG(3) << "t_n_f= " << bitset<8>(type_n_flags);                \
        VLOG(3) << "type=  " << bitset<CELL_TYPE_WIDTH>(type);          \
        VLOG(3) << "flags=    " << bitset<CELL_FLAGS_WIDTH>(flags);     \
        CHECK_EQ(type, exp_t_val);                                      \
        CHECK_EQ(flags, exp_f_val);                                     \
        VLOG(3) << "\n";                                                \
    } while (0)

    TEST_GET(0b00010101, 0b000, 0b10101);
    TEST_GET(0b00110101, 0b001, 0b10101);
    TEST_GET(0b01010101, 0b010, 0b10101);
    TEST_GET(0b01110101, 0b011, 0b10101);
    TEST_GET(0b10010101, 0b100, 0b10101);
    TEST_GET(0b10110101, 0b101, 0b10101);
    TEST_GET(0b11010101, 0b110, 0b10101);
    TEST_GET(0b11110101, 0b111, 0b10101);

    TEST_GET(0b11100000, 0b111, 0b00000);
    TEST_GET(0b11110001, 0b111, 0b10001);
    TEST_GET(0b00011111, 0b000, 0b11111);
    TEST_GET(0b11100001, 0b111, 0b00001);
    TEST_GET(0b11110000, 0b111, 0b10000);

    // test setting

#define TEST_SET(orig_t_n_f_val, t_val, f_val)                          \
    do {                                                                \
        uint8_t __type_n_flags = orig_t_n_f_val;                        \
        const uint8_t o_type = GET_CELL_TYPE(__type_n_flags);           \
        const uint8_t o_flags = GET_CELL_FLAGS(__type_n_flags);         \
        SET_CELL_TYPE(__type_n_flags, t_val);                           \
        SET_CELL_FLAGS(__type_n_flags, f_val);                          \
        const uint8_t n_type = GET_CELL_TYPE(__type_n_flags);           \
        const uint8_t n_flags = GET_CELL_FLAGS(__type_n_flags);         \
        VLOG(3) << "o_type=  " << bitset<CELL_TYPE_WIDTH>(o_type);      \
        VLOG(3) << "n_type=  " << bitset<CELL_TYPE_WIDTH>(n_type);      \
        VLOG(3) << "o_flags=    " << bitset<CELL_FLAGS_WIDTH>(o_flags); \
        VLOG(3) << "n_flags=    " << bitset<CELL_FLAGS_WIDTH>(n_flags); \
        assert(n_type == t_val);                                        \
        assert(n_flags == f_val);                                       \
        VLOG(3) << "\n";                                                \
    } while (0)

    VLOG(3) << "============== test set ==========\n";

    TEST_SET(0b00000000, 0b101, 0b00000);
    TEST_SET(0b11111111, 0b000, 0b10001);
    TEST_SET(0b10101010, 0b010, 0b10101);

    tested = true;

    VLOG(2) << "done";
}
