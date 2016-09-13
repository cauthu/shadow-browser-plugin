
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
    ChannelClosedCb ch_closed_cb,
    NewStreamConnectRequestCb st_connect_req_cb)
    : BufloMuxChannel(fd, is_client_side,
                      ch_closed_cb,
                      st_connect_req_cb)
    , evbase_(evbase)
    , socket_read_ev_(nullptr, event_free)
    , socket_write_ev_(nullptr, event_free)
    , cell_size_(cell_size)
    , cell_body_size_(cell_size_ - (CELL_HEADER_SIZE))
    , defense_active_(false)
    , whole_dummy_cell_at_end_outbuf_(false)
    , buflo_timer_(new Timer(
                       evbase, false,
                       boost::bind(&BufloMuxChannelImplSpdy::_buflo_timer_fired,
                                   this, _1)))
{
    _setup_spdylay_session();

#define ALLOC_EVBUF(buf) \
    do { buf = evbuffer_new(); CHECK_NOTNULL(buf); } while (0)

    ALLOC_EVBUF(spdy_inbuf_);
    ALLOC_EVBUF(spdy_outbuf_);
    ALLOC_EVBUF(cell_inbuf_);
    ALLOC_EVBUF(cell_outbuf_);

    cell_read_info_.reset();

    socket_read_ev_.reset(
        event_new(evbase_, fd_, EV_READ | EV_PERSIST, s_socket_readcb, this));
    socket_write_ev_.reset(
        event_new(evbase_, fd_, EV_WRITE | EV_PERSIST, s_socket_writecb, this));

    auto rv = event_add(socket_read_ev_.get(), nullptr);
    CHECK_EQ(rv, 0);

    // the write event has to be enabled only when we have data to
    // write
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
BufloMuxChannelImplSpdy::create_stream(const char* host,
                                       const in_port_t& port,
                                       BufloMuxChannelStreamObserver *observer)
{
    vlogself(2) << "begin";

    string hostport_str(host);
    hostport_str.append(":");
    hostport_str.append(std::to_string(port));

    // const char **nv = (const char**)calloc(5*2 + 1, sizeof(char*));
    // size_t hdidx = 0;

    // nv[hdidx++] = ":method";
    // nv[hdidx++] = "CONNECT";
    // nv[hdidx++] = ":scheme";
    // nv[hdidx++] = "http";
    // nv[hdidx++] = ":path";
    // nv[hdidx++] = "/";
    // nv[hdidx++] = ":version";
    // nv[hdidx++] = "HTTP/1.1";


    const char **nv = (const char**)calloc(1*2 + 1, sizeof(char*));
    size_t hdidx = 0;
    nv[hdidx++] = ":host";
    nv[hdidx++] = hostport_str.c_str();

    nv[hdidx++] = nullptr;

    /* spdylay will make copies of nv */
    int rv = spdylay_submit_syn_stream(spdysess_, 0, 0, 0, nv, observer);
    CHECK_EQ(rv, 0);

    free(nv);

    _pump_spdy_send();
    if (_maybe_flush_data_to_cell_outbuf()) {
        _maybe_toggle_write_monitoring(true);
    }

    vlogself(2) << "done";
    return 0;
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
BufloMuxChannelImplSpdy::set_stream_connect_result(int sid, bool ok)
{

    // submit the response
    return false;
}

int
BufloMuxChannelImplSpdy::read(int sid, uint8_t *data, size_t len)
{
    return 0;
}

int
BufloMuxChannelImplSpdy::read_buffer(int sid, struct evbuffer* buf, size_t len) {
    return 0;
}

int
BufloMuxChannelImplSpdy::drain(int sid, size_t len) 
{
    return 0;
}

uint8_t* BufloMuxChannelImplSpdy::peek(int sid, ssize_t len) 
{
    return nullptr;
}

struct evbuffer*
BufloMuxChannelImplSpdy::get_input_evbuf(int sid)
{
    return nullptr;
}

size_t
BufloMuxChannelImplSpdy::get_avail_input_length(int sid) const
{
    return 0;
}

int
BufloMuxChannelImplSpdy::write(int sid, const uint8_t *data, size_t len) 
{
    return 0;
}

int
BufloMuxChannelImplSpdy::write_buffer(int sid, struct evbuffer *buf) 
{
    return 0;
}

int
BufloMuxChannelImplSpdy::write_dummy(int sid, size_t len) 
{return 0;}

void
BufloMuxChannelImplSpdy::close_stream(int sid) 
{}

void
BufloMuxChannelImplSpdy::_buflo_timer_fired(Timer* timer)
{
    CHECK(defense_active_);

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
 * THIS WILL NOT toggle socket write monitoring. other code is
 * responsibly for checking spdylay_session_want_write() if interested
 */
void
BufloMuxChannelImplSpdy::_pump_spdy_send()
{
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
}

bool
BufloMuxChannelImplSpdy::_maybe_flush_data_to_cell_outbuf()
{
    CHECK(!defense_active_);
    bool did_write = false;
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

    vlogself(2) << "done";

    return true;
}

void
BufloMuxChannelImplSpdy::_send_cell_outbuf()
{
    vlogself(2) << "begin";

    if (defense_active_) {
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
    CHECK(!defense_active_);
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
    CHECK(defense_active_);

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
BufloMuxChannelImplSpdy::_consume_input()
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

        // loop to process all complete msgs
        switch (cell_read_info_.state_) {
        case ReadState::READ_HEADER:
            VLOG(2) << "trying to read msg type and length";
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
                VLOG(2) << "got type= " << cell_read_info_.type_
                        << ", payload len= " << cell_read_info_.payload_len_;

                // TODO: optimize: if cell type is dummy, do the
                // drain/dropread logic here

                // not draining input buf here!
            } else {
                VLOG(2) << "not enough bytes yet";
                keep_consuming = false; // to break out of loop
            }
            break;
        case ReadState::READ_BODY:
            if (num_avail_bytes >= cell_size_) {

                if (cell_read_info_.type_ != CellType::DUMMY) {
                    // we want the whole cell to be available, but we
                    // only need to make up to the payload, if any,
                    // contiguous

                    const auto need_contiguous_amt =
                        CELL_HEADER_SIZE + cell_read_info_.payload_len_;
                    auto data = evbuffer_pullup(cell_inbuf_, need_contiguous_amt);
                    CHECK_NOTNULL(data);

                    cell_read_info_.payload_ = data + CELL_HEADER_SIZE;

                    _handle_non_dummy_input_cell();
                }

                // drain a whole cell from buf
                auto rv = evbuffer_drain(cell_inbuf_, cell_size_);
                CHECK_EQ(rv, 0);

                cell_read_info_.reset();
            } else {
                VLOG(2) << "not enough bytes yet";
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
BufloMuxChannelImplSpdy::_handle_non_dummy_input_cell()
{
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
            _consume_input();
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
    // depend on socket events
    CHECK(!defense_active_);

    if (what & EV_WRITE) {
        _send_cell_outbuf();
    } else {
        CHECK(0) << "invalid events: " << what;
    }
}

ssize_t
BufloMuxChannelImplSpdy::_on_spdylay_send_cb(
    spdylay_session *session,
    const uint8_t *data,
    size_t length,
    int flags)
{
    vlogself(2) << "begin, length= " << length;
    CHECK_EQ(session, spdysess_);

    ssize_t retval = SPDYLAY_ERR_CALLBACK_FAILURE;

    // add data to the output buf for writing later
    const auto rv = evbuffer_add(spdy_outbuf_, data, length);
    if (rv == 0) {
        retval = length;
    }
    else if (rv == -1) {
        logself(WARNING) << "outbuf didn't accept our add";
        retval = SPDYLAY_ERR_CALLBACK_FAILURE;
    } else {
        logself(FATAL) << "invalid rv= " << rv;
    }
 
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
        const int32_t sid = frame->syn_stream.stream_id;
        auto *observer = (BufloMuxChannelStreamObserver*)
            spdylay_session_get_stream_user_data(session, sid);

        std::unique_ptr<StreamState> sstate(new StreamState());
        const auto ret = stream_states_.insert(
            make_pair(sid, std::move(sstate)));
        CHECK(ret.second); // insist it was newly inserted

        vlogself(2) << "notify that stream id is assigned";
        DestructorGuard dg(this);
        observer->onStreamIdAssigned(this, sid);
    }

    vlogself(2) << "done";
}

void
BufloMuxChannelImplSpdy::_on_spdylay_on_ctrl_recv_cb(spdylay_session *session,
                                                     spdylay_frame_type type,
                                                     spdylay_frame *frame)
{
    vlogself(2) << "begin";

    switch(type) {
    case SPDYLAY_SYN_STREAM: {
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

        string host_str = host_hdr.substr(0, colon_pos + 1);
        string port_str = host_hdr.substr(colon_pos + 1);
        const uint16_t port = boost::lexical_cast<uint16_t>(port_str);

        vlogself(2) << "host= [" << host_hdr << "] port= " << port;

        DestructorGuard dg(this);
        st_connect_req_cb_(this, sid, host_str.c_str(), port);

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
    // callbacks.recv_callback = s_spdylay_recv_cb;
    callbacks.on_ctrl_recv_callback = s_spdylay_on_ctrl_recv_cb;
    // callbacks.on_data_chunk_recv_callback = s_spdylay_on_data_chunk_recv_cb;
    // callbacks.on_data_recv_callback = s_spdylay_on_data_recv_cb;
    // /* callbacks.on_stream_close_callback = spdylay_on_stream_close_cb; */
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

// void
// BufloMuxChannelImplSpdy::s_spdylay_on_ctrl_not_send_cb(
//     spdylay_session *session,
//     spdylay_frame_type type,
//     spdylay_frame *frame,
//     int error_code,
//     void *user_data)
// {
//     auto *conn = (BufloMuxChannelImplSpdy*)(user_data);
//     conn->_on_spdylay_on_ctrl_not_send_cb(session, type, frame, error_code);
// }

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
