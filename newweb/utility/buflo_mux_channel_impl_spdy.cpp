
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <string>

#include "buflo_mux_channel_impl_spdy.hpp"
#include "common.hpp"


using std::string;
using std::vector;
using std::pair;

#define _LOG_PREFIX(inst) << "buflomux= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)



#define CELL_TYPE_FIELD_SIZE 1
#define CELL_PAYLOAD_LEN_FIELD_SIZE 2
#define CELL_HEADER_SIZE ((CELL_TYPE_FIELD_SIZE) + (CELL_PAYLOAD_LEN_FIELD_SIZE))


#define MAYBE_GET_STREAMSTATE(sid, errretval)                           \
    auto streamstate = stream_states_[sid].get();                       \
    do {                                                                \
        if (!streamstate) {                                             \
            logself(ERROR) << "unknown sid: " << sid;                   \
            /* std::map just created an empty unique_ptr */             \
            /* so we erase it */                                        \
            stream_states_.erase(sid);                                  \
            return errretval;                                           \
        }                                                               \
    } while(0)


namespace myio { namespace buflo
{

/*
  cells are fixed size. each contains a header and a body. the header
  has 1-byte type field and a 2-byte length field. the length field
  specifies the number of useful data bytes at the front of the body,
  and the remaining bytes at the end of the body, if any, are dummy
  bytes

  XXX/the way we have it right now, we don't really need the DUMMY
  cell type because we can just have a DATA cell with a zero payload
  length, which is equivalent to a fully dummy cell. having a separate
  dummy cell type is useful if, for example, for a dummy cell, we can
  skip the length field


  in defense mode: cell_outbuf_ is to construct one cell to send to
  socket. now, the socket might accept only a part of the write, then
  the cell outbuf will still contain some bytes: all of those bytes
  are part of the cell that has been partially writen and thus have to
  be fully written
*/

BufloMuxChannelImplSpdy::BufloMuxChannelImplSpdy(
    struct event_base* evbase,
    int fd, bool is_client_side,
    size_t cell_size,
    ChannelReadyCb ch_ready_cb,
    ChannelClosedCb ch_closed_cb,
    NewStreamConnectRequestCb st_connect_req_cb)
    : BufloMuxChannel(fd, is_client_side,
                      ch_closed_cb,
                      st_connect_req_cb)
    , evbase_(evbase)
    , socket_read_ev_(nullptr, event_free)
    , socket_write_ev_(nullptr, event_free)
    , ch_ready_cb_(ch_ready_cb)
    , cell_size_(cell_size)
    , cell_body_size_(cell_size_ - (CELL_HEADER_SIZE))
    , peer_cell_size_(0)
    , peer_cell_body_size_(0)
    , defense_info_{0}
    , whole_dummy_cell_at_end_outbuf_(false)
{
    // for now, restrict set of possible sizes
    CHECK((cell_size_ == 512) ||
          (cell_size_ == 1024) ||
          (cell_size_ == 1500));

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

    // we should be able to send our 2-byte cell size now, to avoid
    // having to use a new State enum value and set up an EV_WRITE
    // event, etc.

    const uint16_t cs = htons(cell_size_);
    auto rv = write(fd_, (uint8_t*)&cs, sizeof cs);
    CHECK_EQ(rv, sizeof cs);

    struct timeval timeout;
    timeout.tv_sec = 4;
    timeout.tv_usec = 0;
    // set up a one-time event to read peer's cell size, and we will
    // then enable the regular read event
    rv = event_base_once(evbase_, fd_, EV_READ|EV_TIMEOUT,
                         s_read_peer_cell_size, this, &timeout);
    CHECK_EQ(rv, 0);
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
BufloMuxChannelImplSpdy::start_defense_session(const uint16_t& frequencyMs,
                                               const uint16_t& durationSec)
{
    CHECK(!defense_info_.active) << "currently only support starting session when none is active";

    CHECK((frequencyMs == 10)
          || (frequencyMs == 20)
          || (frequencyMs == 50)
        ); // TODO: support more

    struct timeval current_tv;
    auto rv = gettimeofday(&current_tv, nullptr);
    CHECK_EQ(rv, 0);

    struct timeval duration_tv;
    duration_tv.tv_sec = durationSec;
    duration_tv.tv_usec = 0;

    evutil_timeradd(&current_tv, &duration_tv, &defense_info_.until);

    struct timeval freq_tv = {0};
    freq_tv.tv_usec = (frequencyMs * 1000);

    buflo_timer_->start(&freq_tv);

    defense_info_.active = true;
    return true;
}

void
BufloMuxChannelImplSpdy::stop_defense_session()
{
    CHECK(defense_info_.active); // for now

    buflo_timer_->cancel();
    defense_info_.reset();
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

// int
// BufloMuxChannelImplSpdy::read(int sid, uint8_t *data, size_t len)
// {
//     logself(FATAL) << "to implement";
//     return 0;
// }

int
BufloMuxChannelImplSpdy::read_buffer(int sid, struct evbuffer* buf, size_t len)
{
    MAYBE_GET_STREAMSTATE(sid, -1);

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

uint8_t* BufloMuxChannelImplSpdy::peek(int sid, ssize_t len) 
{
    return nullptr;
}

struct evbuffer*
BufloMuxChannelImplSpdy::get_input_evbuf(int sid)
{
    MAYBE_GET_STREAMSTATE(sid, nullptr);

    // return the buf that user can read their new input data, i.e.,
    // outward data.  XXX/maybe rename function

    return streamstate->outward_buf_;
}

size_t
BufloMuxChannelImplSpdy::get_avail_input_length(int sid) const
{
    if (inMap(stream_states_, sid)) {
        return evbuffer_get_length(stream_states_.at(sid)->outward_buf_);
    } else {
        return 0;
    }
}

// int
// BufloMuxChannelImplSpdy::write(int sid, const uint8_t *data, size_t len)
// {
//     logself(FATAL) << "to implement";
//     return -1;
// }

int
BufloMuxChannelImplSpdy::write_buffer(int sid, struct evbuffer *buf) 
{
    MAYBE_GET_STREAMSTATE(sid, -1);

    auto rv = evbuffer_add_buffer(
        /* dst */ streamstate->inward_buf_,
        /* src */ buf);
    CHECK_EQ(rv, 0);

    if (streamstate->inward_deferred_) {
        rv = spdylay_session_resume_data(spdysess_, sid);
        CHECK_EQ(rv, 0);
        streamstate->inward_deferred_ = false;
    }

    _pump_spdy_send();

    return 0;
}

// int
// BufloMuxChannelImplSpdy::write_dummy(int sid, size_t len)
// {
//     logself(FATAL) << "to implement";
//     return 0;
// }

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
    CHECK(defense_info_.active);

    struct timeval current_tv;
    const auto rv = gettimeofday(&current_tv, nullptr);
    CHECK_EQ(rv, 0);

    if (evutil_timercmp(&current_tv, &defense_info_.until, >=)) {
        vlogself(2) << "we're done defending, toggle write monitoring instead";
        defense_info_.reset();
        buflo_timer_->cancel();
        // not forcing
        _maybe_toggle_write_monitoring(false);
        return;
    }

    vlogself(2) << "begin";

    if (evbuffer_get_length(cell_outbuf_) >= cell_size_) {
        // there is already one or more cell's worth of bytes waiting
        // to be sent, so we just try to send it and return
        _send_cell_outbuf();
        return;
    }

    // control has priority over data
    if (!_maybe_add_control_cell_to_outbuf()) {
        // no control things to add, so we can add spdy data
        _pump_spdy_send();
        if (!_maybe_add_data_cell_to_outbuf()) {
            // not even data, so add dummy
            _ensure_a_whole_dummy_cell_at_end_outbuf();
        }
    }

    // there must be at least one cell's worth of bytes in the outbuf
    DCHECK_GE(evbuffer_get_length(cell_outbuf_), cell_size_);

    _send_cell_outbuf();

    vlogself(2) << "done";
}

/*
 * after telling spdy to write to its buf, if defense is not active,
 * then will enable write monitoring so we can actually send to peer
 * asap
 */
void
BufloMuxChannelImplSpdy::_pump_spdy_send()
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

    if (!defense_info_.active) {
        vlogself(2) << "maybe flush to cell outbuf";
        if (_maybe_flush_data_to_cell_outbuf()) {
            _maybe_toggle_write_monitoring(true);
        }
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

bool
BufloMuxChannelImplSpdy::_maybe_flush_data_to_cell_outbuf()
{
    CHECK(!defense_info_.active);
    bool did_write = false;
    vlogself(2) << "begin, spdy outbuf len: "
                << evbuffer_get_length(spdy_outbuf_);
    while (evbuffer_get_length(spdy_outbuf_)) {
        const auto rv = _maybe_add_data_cell_to_outbuf();
        CHECK(rv);
        did_write = true;
    }
    vlogself(2) << "returning " << did_write;
    return did_write;
}

/*
 * possibly add ONE cell of spdy data to the cell outbuf
 */
bool
BufloMuxChannelImplSpdy::_maybe_add_data_cell_to_outbuf()
{
    static const uint8_t type_field = CellType::DATA;

    /* append a data cell if there's data in spdy_outbuf_ */

    const size_t payload_len = std::min(
        cell_body_size_,
        evbuffer_get_length(spdy_outbuf_));
    const uint16_t len_field = htons(payload_len);

    if (payload_len == 0) {
        return false;
    }

    // add type and length
    auto rv = evbuffer_add(
        cell_outbuf_, (const uint8_t*)&type_field, sizeof type_field);
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

    return true;
}

void
BufloMuxChannelImplSpdy::_send_cell_outbuf()
{
    vlogself(2) << "begin";

    if (defense_info_.active) {
        // now tell socket to write ONE cell's worth of bytes

        // there must be at least that much data in outbuf
        auto curbufsize = evbuffer_get_length(cell_outbuf_);
        CHECK_GE(curbufsize, cell_size_);

        const auto rv = evbuffer_write_atmost(cell_outbuf_, fd_, cell_size_);
        vlogself(2) << "evbuffer_write_atmost() return: " << rv;

        if (rv > 0) {
            // rv is number of bytes written
            curbufsize -= rv;
            vlogself(2) << "remaining buf size: " << curbufsize;
            if (curbufsize < cell_size_) {
                // we can blindly clear this flag: if there was one
                // whole dummy cell, then it's no longer true because
                // cur buf size is less than one whole cell
                whole_dummy_cell_at_end_outbuf_ = false;
            }
        } else {
            _handle_failed_socket_io("write", rv, false);
        }
    } else {
        // now tell socket to write as much as possible
        const auto rv = evbuffer_write_atmost(cell_outbuf_, fd_, -1);
        vlogself(2) << "evbuffer_write_atmost() return: " << rv;
        _maybe_toggle_write_monitoring();
        if (rv <= 0) {
            _handle_failed_socket_io("write", rv, true);
        }
    }

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::_maybe_toggle_write_monitoring(bool force_enable)
{
    CHECK(!defense_info_.active);
    dvlogself(2) << "force= " << force_enable
                 << ", cell_outbuf_ len= " << evbuffer_get_length(cell_outbuf_);
    if (force_enable || evbuffer_get_length(cell_outbuf_)) {
        // there is some output data to write, or we are being forced,
        // then start monitoring
        auto rv = event_add(socket_write_ev_.get(), nullptr);
        CHECK_EQ(rv, 0);
    } else {
        // stop monitoring
        auto rv = event_del(socket_write_ev_.get());
        CHECK_EQ(rv, 0);
    }
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
BufloMuxChannelImplSpdy::_ensure_a_whole_dummy_cell_at_end_outbuf()
{
    CHECK(defense_info_.active);

    // if there's already one dummy cell at the end of outbuf then we
    // don't want to add more
    vlogself(2) << "whole_dummy_cell_at_end_outbuf_= "
                << whole_dummy_cell_at_end_outbuf_;
    if (whole_dummy_cell_at_end_outbuf_) {
        vlogself(2) << "no need to add dummy cell";
        return;
    }

    static const uint8_t type_field = CellType::DUMMY;
    static const uint16_t len_field = 0;

    // add type and length
    auto rv = evbuffer_add(
        cell_outbuf_, (const uint8_t*)&type_field, sizeof type_field);
    CHECK_EQ(rv, 0);
    rv = evbuffer_add(
        cell_outbuf_, (const uint8_t*)&len_field, sizeof len_field);
    CHECK_EQ(rv, 0);

    rv = evbuffer_add_reference(
        cell_outbuf_, common::static_bytes->c_str(),
        cell_body_size_, nullptr, nullptr);

    whole_dummy_cell_at_end_outbuf_ = true;
    vlogself(2) << "added one dummy cell";
}

void
BufloMuxChannelImplSpdy::_read_cells()
{
    vlogself(2) << "begin";

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

                memcpy((uint8_t*)&cell_read_info_.type_,
                       buf, CELL_TYPE_FIELD_SIZE);
                memcpy((uint8_t*)&cell_read_info_.payload_len_,
                       buf + CELL_TYPE_FIELD_SIZE, CELL_PAYLOAD_LEN_FIELD_SIZE);

                // convert to host byte order
                cell_read_info_.payload_len_ = ntohs(cell_read_info_.payload_len_);

                // update state
                cell_read_info_.state_ = ReadState::READ_BODY;
                vlogself(2) << "got type= " << unsigned(cell_read_info_.type_)
                            << ", payload len= " << cell_read_info_.payload_len_;

                // TODO: optimize: if cell type is dummy, do the
                // drain/dropread logic here

                // not draining input buf here!
            } else {
                vlogself(2) << "not enough bytes yet";
                keep_consuming = false; // to break out of loop
            }
            break;
        case ReadState::READ_BODY:
            if (num_avail_bytes >= peer_cell_size_) {

                if (cell_read_info_.type_ != CellType::DUMMY) {
                    // TODO/ handle_non_dummy_input_cell will pump
                    // spdy recv on the cell; maybe we consider
                    // pumping spdy recv only once we have processed
                    // all data cells available in the cell inbuf
                    _handle_non_dummy_input_cell(cell_read_info_.payload_len_);
                } else {
                    // drain a whole cell from buf
                    auto rv = evbuffer_drain(cell_inbuf_, peer_cell_size_);
                    CHECK_EQ(rv, 0);
                }

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
BufloMuxChannelImplSpdy::_handle_non_dummy_input_cell(size_t payload_len)
{
    /*
     * the whole cell, including header, is available at the front of
     * the cell_inbuf_, but it might not be contiguous---call
     * evbuffer_pullup() if you need
     *
     * responsible for removing the cell from the input buf
     */

    CHECK_GT(payload_len, 0);
    CHECK_LE(payload_len, peer_cell_size_);

    auto ptr = evbuffer_pullup(cell_inbuf_, CELL_HEADER_SIZE + payload_len);

    switch (cell_read_info_.type_) {
    case CellType::DATA: {
        auto rv = evbuffer_add(spdy_inbuf_, ptr+CELL_HEADER_SIZE, payload_len);
        CHECK_EQ(rv, 0);
        _pump_spdy_recv();
        break;
    }
    default:
        logself(FATAL) << "not reached";
    }

    // now drain the whole cell
    auto rv = evbuffer_drain(cell_inbuf_, peer_cell_size_);
    CHECK_EQ(rv, 0);

    return;
}

void
BufloMuxChannelImplSpdy::_on_socket_eof()
{
    return;
}

void
BufloMuxChannelImplSpdy::_on_socket_error()
{
    return;
}

void
BufloMuxChannelImplSpdy::_on_socket_readcb(int fd, short what)
{
    vlogself(2) << "begin";

    if (what & EV_READ) {
        // let buffer decide how much to read
        const auto rv = evbuffer_read(cell_inbuf_, fd_, -1);
        vlogself(2) << "evbuffer_read() returns: " << rv;
        if (rv > 0) {
            // there's new data
            _read_cells();
        } else {
            _handle_failed_socket_io("read", rv, true);
        }
    } else {
        CHECK(0) << "invalid events: " << what;
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
    CHECK(!defense_info_.active);

    vlogself(2) << "begin";

    if (what & EV_WRITE) {
        _send_cell_outbuf();
    } else {
        CHECK(0) << "invalid events: " << what;
    }

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::s_read_peer_cell_size(int, short what, void *arg)
{
    auto ch = (BufloMuxChannelImplSpdy*)arg;
    ch->_on_read_peer_cell_size(what);
}

void
BufloMuxChannelImplSpdy::_on_read_peer_cell_size(short what)
{
    if (what & EV_TIMEOUT) {
        logself(FATAL) << "timeout reading peer cell size";
    } else {
        CHECK(what & EV_READ);
        auto rv = read(fd_, &peer_cell_size_, sizeof peer_cell_size_);
        CHECK_EQ(rv, 2);
        peer_cell_size_ = ntohs(peer_cell_size_);
        vlogself(2) << "peer using cell size: " << peer_cell_size_;

        peer_cell_body_size_ = peer_cell_size_ - CELL_HEADER_SIZE;

        // now we can enable the regular read event
        rv = event_add(socket_read_ev_.get(), nullptr);
        CHECK_EQ(rv, 0);

        DestructorGuard dg(this);
        ch_ready_cb_(this);
    }
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
    vlogself(2) << "begin, sid= " << stream_id;
    auto *observer = (BufloMuxChannelStreamObserver*)
                     spdylay_session_get_stream_user_data(session, stream_id);
    if (observer) {
        observer->onStreamClosed(this);
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
        DestructorGuard dg(this);
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
    const auto rv = spdylay_submit_data(spdysess_, sid, 0, &data_provider);
    CHECK_EQ(rv, 0);
}

void
BufloMuxChannelImplSpdy::_on_spdylay_on_ctrl_recv_cb(spdylay_session *session,
                                                     spdylay_frame_type type,
                                                     spdylay_frame *frame)
{
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
    // callbacks.on_data_recv_callback = s_spdylay_on_data_recv_cb;
    callbacks.on_stream_close_callback = s_spdylay_on_stream_close_cb;
    callbacks.before_ctrl_send_callback = s_spdylay_before_ctrl_send_cb;
    // callbacks.on_ctrl_not_send_callback = s_spdylay_on_ctrl_not_send_cb;

    spdylay_session *session = nullptr;

    // version 3 just adds flow control, compared to version 2
    auto rv = 0;
    if (is_client_side_) {
        rv = spdylay_session_client_new(&session, 3, &callbacks, this);
    } else {
        rv = spdylay_session_server_new(&session, 3, &callbacks, this);
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
    MAYBE_GET_STREAMSTATE(stream_id, );

    const auto rv = evbuffer_add(streamstate->outward_buf_, data, len);
    CHECK_NE(rv, -1);

    auto *observer = (BufloMuxChannelStreamObserver*)
                     spdylay_session_get_stream_user_data(session, stream_id);
    if (observer) {
        observer->onStreamNewDataAvailable(this);
    } else {
        logself(WARNING) << "no observer for sid " << stream_id
                         << " to notify of new available data";
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

    MAYBE_GET_STREAMSTATE(stream_id, SPDYLAY_ERR_TEMPORAL_CALLBACK_FAILURE);

    const auto rv = evbuffer_remove(streamstate->inward_buf_, buf, length);
    CHECK_NE(rv, -1);

    if (rv > 0) {
        // able to read some bytes
        retval = rv;
    } else {
        CHECK_EQ(rv, 0);
        vlogself(2) << "nothing to read, so we defer";
        streamstate->inward_deferred_ = true;
        retval = SPDYLAY_ERR_DEFERRED;
    }

    vlogself(2) << "done, returning: " << retval;
    return retval;
}

BufloMuxChannelImplSpdy::~BufloMuxChannelImplSpdy()
{
    if (fd_) {
        ::close(fd_);
        fd_ = -1;
    }

#define FREE_EVBUF(buf)                         \
    do {                                        \
        if (buf) {                              \
            evbuffer_free(buf);                 \
            buf = nullptr;                      \
        }                                       \
    } while (0)

    FREE_EVBUF(spdy_inbuf_);
    FREE_EVBUF(spdy_outbuf_);
    FREE_EVBUF(cell_inbuf_);
    FREE_EVBUF(cell_outbuf_);

    // TODO: free the stream states
}

} // end namespace buflo
} // end namespace myio
