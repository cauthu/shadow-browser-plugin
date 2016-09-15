
#include <string.h>
#include <algorithm>
#include <vector>

#include <boost/lexical_cast.hpp>

#include "connection.hpp"
#include "../easylogging++.h"
#include "../common.hpp"

#ifdef __cplusplus /* If this is a C++ compiler, use C linkage */
extern "C" {
#endif

#include "http_parse.h"

#ifdef __cplusplus /* If this is a C++ compiler, end C linkage */
}
#endif

using std::vector;
using std::string;
using std::pair;
using myio::Socks5Connector;
using myio::Socks5ConnectorObserver;
using myio::StreamChannel;
using myio::TCPChannel;

using boost::lexical_cast;


#define _LOG_PREFIX(inst) << "cnx= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)



Connection::Connection(
    struct event_base *evbase,
    const in_addr_t& addr, const in_port_t& port,
    const in_addr_t& socks5_addr, const in_port_t& socks5_port,
    const in_addr_t& ssp_addr, const in_port_t& ssp_port,
    ConnectionErrorCb error_cb, ConnectionEOFCb eof_cb,
    PushedMetaCb pushed_meta_cb, PushedBodyDataCb pushed_body_data_cb,
    PushedBodyDoneCb pushed_body_done_cb,
    void *cb_data, const bool& use_spdy
    )
    : use_spdy_(use_spdy), evbase_(evbase)
    , state_(State::DISCONNECTED)
    , addr_(addr), port_(port)
    , socks5_addr_(socks5_addr), socks5_port_(socks5_port)
    , ssp_addr_(ssp_addr), ssp_port_(ssp_port)
    , cnx_error_cb_(error_cb), cnx_eof_cb_(eof_cb)
    , notify_pushed_meta_(pushed_meta_cb)
    , notify_pushed_body_data_(pushed_body_data_cb)
    , notify_pushed_body_done_(pushed_body_done_cb)
    , spdysess_{nullptr, spdylay_session_del}
    , http_rsp_state_(HTTPRespState::HTTP_RSP_STATE_STATUS_LINE)
    , http_rsp_status_(-1), remaining_resp_body_len_(0)
    , cumulative_num_sent_bytes_(0), cumulative_num_recv_bytes_(0)
{
    /* ssp acts as an http proxy, only it uses spdy to transport. so
     * if ssp is used, then we don't need the actual address of the
     * final site, as the ":host" header will take care of
     * that. otherwise, i.e., no ssp host is specified, then we need
     * to the final site's address.
     */
    if (ssp_addr_) {
        CHECK(!addr_);
    } else {
        CHECK(addr_);
    }

    if (use_spdy_) {
        /* it's ok to set up the session now even though socket is not
         * connected */
        CHECK(!spdysess_);
        _setup_spdylay_session();
        CHECK(spdysess_);
    }

    initiate_connection();
}

void
Connection::_maybe_http_write_to_transport()
{
    vlogself(2) << "begin";

    if (submitted_req_queue_.empty()) {
        vlogself(2) << "submit queue is empty -> nothing to do";
        return;
    }

    auto const qsize = active_req_queue_.size();
    vlogself(2) << "active req qsize " << qsize;
    CHECK_LE(qsize, 1);

    if (qsize > 0) {
        vlogself(2) << "there's an active req -> can't write to output";
        return;
    }

    vlogself(2) << "writing a req to transport";

    auto req = submitted_req_queue_.front();
    CHECK_NOTNULL(req);
    submitted_req_queue_.pop_front();

    std::unique_ptr<struct evbuffer, void(*)(struct evbuffer*)> buf(
        evbuffer_new(), evbuffer_free);
    CHECK_NOTNULL(buf.get());

    // add request line and some headers. need to fill out
    // content-length value a lil later
    auto rv = evbuffer_add_printf(
        buf.get(),
        "%s\r\n"
        "%s: %zu\r\n"
        "%s: %zu\r\n"
        "%s: ",
        common::http::request_line,
        common::http::resp_meta_size_name, req->exp_resp_meta_size(),
        common::http::resp_body_size_name, req->exp_resp_body_size(),
        common::http::content_length_name
        );
    CHECK_GT(rv, 0);

    /* figure out how many opaque payload bytes we can send */
    const auto already_used_size = evbuffer_get_length(buf.get());
    CHECK_GT(req->req_total_size(), already_used_size);
    auto num_opaque_bytes_avail = req->req_total_size() - already_used_size;

    vlogself(2) << "num_opaque_bytes_avail= " << num_opaque_bytes_avail;

    // complete the content length header and overall headers
    rv = evbuffer_add_printf(
        buf.get(), "%zu\r\n\r\n", num_opaque_bytes_avail);
    CHECK_GT(rv, 0);

    /* now can give buf to transport */
    rv = transport_->write_buffer(buf.get());
    CHECK_EQ(rv, 0);
    CHECK_EQ(evbuffer_get_length(buf.get()), 0);

    vlogself(2) << "now for the body we can tell transport to use dummy bytes";
    rv = transport_->write_dummy(num_opaque_bytes_avail);
    CHECK_EQ(rv, 0);

    active_req_queue_.push(req);

    // set the read size hint to the approximate expected size of meta
    // info of the response
    transport_->set_read_size_hint(req->exp_resp_meta_size());

    vlogself(2) << "done";
    return;
}

int
Connection::submit_request(Request* req)
{
    /* it's ok to submit requests if not yet connected, etc, but proly
     * a bug if submitting a request after we have closed
     */
    CHECK((state_ != State::NO_LONGER_USABLE) && (state_ != State::DESTROYED));

    vlogself(2) << "begin";

    if (use_spdy_) {
        const auto& hdrs = req->get_headers();
        const char **nv = (const char**)calloc(5*2 + hdrs.size()*2 + 1, sizeof(char*));
        size_t hdidx = 0;
        nv[hdidx++] = ":method";
        nv[hdidx++] = "GET";
        nv[hdidx++] = ":path";
        nv[hdidx++] = common::http::request_path;
        nv[hdidx++] = ":version";
        nv[hdidx++] = "HTTP/1.1";
        nv[hdidx++] = ":host";
        nv[hdidx++] = req->host_.c_str();
        nv[hdidx++] = ":scheme";
        nv[hdidx++] = "http";

        string resp_meta_size_val_str = std::to_string(req->exp_resp_meta_size());
        string resp_body_size_val_str = std::to_string(req->exp_resp_body_size());

        nv[hdidx++] = common::http::resp_meta_size_name;
        nv[hdidx++] = resp_meta_size_val_str.c_str();

        nv[hdidx++] = common::http::resp_body_size_name;
        nv[hdidx++] = resp_body_size_val_str.c_str();

        CHECK(0) << "to implement!";

        nv[hdidx++] = nullptr;

        /* spdylay_submit_request() will make copies of nv */
        int rv = spdylay_submit_request(spdysess_.get(), 0, nv, nullptr, req);
        CHECK_EQ(rv, 0);
        free(nv);
    } else {
        submitted_req_queue_.push_back(req);
    }

    if (state_ == State::CONNECTED) {
        _maybe_send();
    }

    req->conn = this;

    vlogself(2) << "done";
    return 0;
}

Connection::~Connection()
{
    vlogself(2) << "begin destructor";
    disconnect();
    evbase_ = nullptr; // no freeing
    vlogself(2) << "done destructor";
}

std::queue<Request*>
Connection::get_active_request_queue() const
{
    return active_req_queue_;
}

std::deque<Request*>
Connection::get_pending_request_queue() const
{
    return submitted_req_queue_;
}

void
Connection::set_request_done_cb(ConnectionRequestDoneCb cb)
{
    notify_request_done_cb_ = cb;
}

bool
Connection::is_idle() const {
    if (state_ == State::CONNECTED) {
        if (use_spdy_) {
            return (!spdylay_session_want_read(spdysess_.get())
                    && !spdylay_session_want_write(spdysess_.get()));
        } else {
            const int subqsize = submitted_req_queue_.size();
            const int activeqsize = active_req_queue_.size();
            vlogself(2) << subqsize << ", " << activeqsize;
            return (!subqsize && !activeqsize);
        }
    }
    return false;
}

void
Connection::disconnect()
{
    vlogself(2) << "begin";
    // CHECK(0 == spdylay_submit_goaway(spdysess_, 0));
    // send_();

    /* Connection::spdylay_recv_cb() is failling the session ==
     * spdysess_ assert during some cleanup. let's try to kill the
     * event first, so it doesn't continue to notify
     * on_read(). UPDATE: doesn't help, so i'll just disable the
     * assert, and simply do nothing if the check fails.
     */
    transport_.reset(); // reset() here instead of waiting for
                        // Connection's destructor

    state_ = State::NO_LONGER_USABLE;
    vlogself(2) << "done";
}

void
Connection::_maybe_http_consume_input()
{
    vlogself(2) << "begin";

    if (active_req_queue_.empty()) {
        vlogself(2) << "no active req waiting to be received";
        return;
    }

    char *line = nullptr;
    bool keep_consuming = true;

    // don't free this inbuf
    struct evbuffer *inbuf = transport_->get_input_evbuf();
    CHECK_NOTNULL(inbuf);

    vlogself(2) << "num bytes available in inbuf: " << evbuffer_get_length(inbuf);

    if (evbuffer_get_length(inbuf) == 0) {
        vlogself(2) << "no input available";
        return;
    }

    /*
     * our use of evbuffer_readln() might be sub-optimal if data comes
     * slowly, e.g., extreme case is one byte at a time, and thus the
     * stream will notify us on every byte and we try readln() every
     * time from begining of inputbuf. in that case, it would be best
     * to:
     *
     * (1) set read low-water mark (but could be a problem if we set
     * too high and input buf never reaches and we don't get notified
     * --> would either need to set conservative value or need some
     * kind of timeout mechanism); and/or
     *
     * (2) remember where in the input buf we stopped searching, so
     * that next time we can start from where we left off instead of
     * from beginning of input buf (would use evbuffer_search_eol(),
     * which is what evbuffer_readln()).
     *
     */

    do {
        line = nullptr;
        switch (http_rsp_state_) {
        case HTTPRespState::HTTP_RSP_STATE_STATUS_LINE: {
            /* readln() DOES drain the buffer if it returns a line */

            size_t line_len = 0;
            line = evbuffer_readln(
                inbuf, &line_len, EVBUFFER_EOL_CRLF_STRICT);
            if (line) {
                vlogself(2) << "got status line: [" << line << "]";

                CHECK_EQ(line_len, common::http::resp_status_line_len);
                DCHECK(!strcmp(line, common::http::resp_status_line));

                http_rsp_state_ = HTTPRespState::HTTP_RSP_STATE_HEADERS;
                free(line);
                line = nullptr;
            } else {
                // not enough input data to find whole status line
                keep_consuming = false;
            }

            break; // out of switch
        }

        case HTTPRespState::HTTP_RSP_STATE_HEADERS: {
            vlogself(2) << "try to get response headers";
            size_t line_len = 0;
            while (nullptr != (line = evbuffer_readln(
                                   inbuf, &line_len, EVBUFFER_EOL_CRLF_STRICT)))
            {
                /* a header line is assumped to be:

                   <name>: <value>

                   the "line" returned by evbuffer_readln() needs to be
                   freed, but we avoid allocating more memory by having
                   our name and value pointers point to the same
                   "line". so when we free, only free the name pointers.
                */
                if (line[0] == '\0') {
                    // the end of headers, so we notify user of
                    // response meta info and then maybe move to next
                    // state to read body

                    free(line);
                    line = nullptr;

                    CHECK_GE(remaining_resp_body_len_, 0);
                    CHECK(0 == (rsp_hdrs_.size() % 2));

                    // notify user of response meta info

                    char **nv = (char**)malloc((rsp_hdrs_.size() + 1) * sizeof (char*));
                    int j = 0;
                    for (; j < rsp_hdrs_.size(); ++j) {
                        nv[j] = rsp_hdrs_[j];
                        dvlogself(2) << "nv[" << j << "] = [" << nv[j] << "]";
                    }
                    nv[j] = nullptr; /* null sentinel */
                    Request *req = active_req_queue_.front();
                    req->notify_rsp_meta(200, nv);
                    free(nv);
                    for (int i = 0; i < rsp_hdrs_.size(); i += 2) {
                        free(rsp_hdrs_[i]);
                    }
                    rsp_hdrs_.clear();

                    // what's next?
                    if (remaining_resp_body_len_ > 0) {
                        // there's a req body we need to consume
                        http_rsp_state_ = HTTPRespState::HTTP_RSP_STATE_BODY;
                    } else {
                        // no response body
                        _done_with_resp();
                    }

                    break; // out of while loop trying to read header lines
                } else {
                    dvlogself(2) << "whole rsp hdr line: [" << line << "]";
                    // XXX/TODO: expect all lower case
                    char *tmp = strchr(line, ':');
                    CHECK(tmp);
                    rsp_hdrs_.push_back(line);
                    *tmp = '\0'; /* colon becomes NULL */
                    ++tmp;
                    *tmp = '\0'; /* blank space becomes NULL */
                    ++tmp;
                    rsp_hdrs_.push_back(tmp);

                    const auto name_str = line;
                    const auto value_str = tmp;

                    vlogself(2) << "rsp hdr name: [" << name_str << "]";

                    if (!strcasecmp(name_str, common::http::content_length_name)) {
                        try {
                            remaining_resp_body_len_ = lexical_cast<size_t>(value_str);
                        } catch (const boost::bad_lexical_cast&) {
                            logself(FATAL) << "bad header value: " << value_str;
                        }

                        vlogself(2) << "body content length: " << remaining_resp_body_len_;
                    }
                    // DO NOT free line. because it's in the rsp_hdrs_;
                    line = nullptr;
                    // done processing one header line
                }
            } // end while loop

            CHECK(!line);
            if (http_rsp_state_ == HTTPRespState::HTTP_RSP_STATE_HEADERS) {
                // if we're still trying to get more headers, that
                // means there was not enough input
                keep_consuming = false;
            }
            break; // out of switch
        }

        case HTTPRespState::HTTP_RSP_STATE_BODY: {
            CHECK(remaining_resp_body_len_ > 0);
            vlogself(2) << "get rsp body, remaining_resp_body_len_ "
                        << remaining_resp_body_len_;

            const auto buflen = evbuffer_get_length(inbuf);
            const auto drain_len = std::min(buflen, remaining_resp_body_len_);
            vlogself(2) << buflen << ", " << remaining_resp_body_len_ << ", "
                        << drain_len;
            if (drain_len > 0) {
                auto rv = evbuffer_drain(inbuf, drain_len);
                CHECK_EQ(rv, 0);
                _got_a_chunk_of_resp_body(drain_len);
            }

            if (remaining_resp_body_len_ > 0) {
                CHECK_EQ(drain_len, buflen);
                CHECK_EQ(evbuffer_get_length(inbuf), 0);
                keep_consuming = false;
                vlogself(2) << "tell channel to drop "
                            << remaining_resp_body_len_ << " future bytes";
                transport_->drop_future_input(
                    this, remaining_resp_body_len_, true);
            } else {
                // have fully received resp body
                /* no pipelining, so input buf should also be empty */
                CHECK_EQ(evbuffer_get_length(inbuf), 0);
                _done_with_resp();
            }

            break;
        }
        default:
            logself(FATAL) << "invalid http_rsp_state_: "
                       << common::as_integer(http_rsp_state_);
            break;
        }
    } while (keep_consuming);

    return;
}

void
Connection::onInputBytesDropped(StreamChannel* ch, size_t len) noexcept
{
    CHECK_EQ(transport_.get(), ch);
    _got_a_chunk_of_resp_body(len);
}

void
Connection::_got_a_chunk_of_resp_body(size_t len)
{
    vlogself(2) << "begin, len: " << len;
    CHECK_GE(remaining_resp_body_len_, len);
    remaining_resp_body_len_ -= len;

    DestructorGuard dg(this);

    vlogself(2) << "notify request of recv'ed resp body chunk";
    Request *req = active_req_queue_.front();
    req->notify_rsp_body_data(nullptr, len);

    if (remaining_resp_body_len_ == 0) {
        vlogself(2) << "notify request we're done receving resp body";
        _done_with_resp();
    }

    vlogself(2) << "done";
}

void
Connection::_done_with_resp()
{
    CHECK_EQ(remaining_resp_body_len_, 0);

    Request *req = active_req_queue_.front();

    /* remove req from active queue before notifying */
    active_req_queue_.pop();

    DestructorGuard dg(this);
    req->notify_rsp_done();

    http_rsp_state_ = HTTPRespState::HTTP_RSP_STATE_STATUS_LINE;
    if (!notify_request_done_cb_.empty()) {
        notify_request_done_cb_(this, req);
    }
    /* if there's more in the submitted queue, we should be
     * able move some into the active queue, now that we just
     * cleared some space in the active queue
     */
    _maybe_http_write_to_transport();
}

void
Connection::onWrittenData(StreamChannel* ch) noexcept
{
    CHECK_EQ(transport_.get(), ch);

    /* we expect this callback only when output buffer has emptied,
     * i.e., write low-water mark is zero */
    CHECK_EQ(transport_->get_output_length(), 0);

    // XXX/should we call _maybe_send() here?
    _maybe_send();
}

void
Connection::onNewReadDataAvailable(StreamChannel* ch) noexcept
{
    CHECK_EQ(transport_.get(), ch);

    int rv = 0;
    vlogself(2) << "begin";

    if (use_spdy_) {
        spdylay_session* session = spdysess_.get();
        if((rv = spdylay_session_recv(session)) != 0) {
            disconnect();
            if (SPDYLAY_ERR_EOF == rv) {
                vlogself(2) << "remote peer closed";
            } else {
                logself(WARNING) << "spdylay_session_recv() returned \""
                             << spdylay_strerror(rv) << "\"";
            }
            return;
        } else if((rv = spdylay_session_send(session)) < 0) {
            logself(FATAL) << "not reached";
        }
        if(rv == 0) {
            if(spdylay_session_want_read(session) == 0 &&
               spdylay_session_want_write(session) == 0) {
                logself(FATAL) << "not reached";
                rv = -1;
            }
        }
    } else {
        _maybe_http_consume_input();
    }
    vlogself(2) << "done";
}

void
Connection::onEOF(StreamChannel*) noexcept
{
    DestructorGuard dg(this);
    cnx_eof_cb_(this);
}

void
Connection::onError(StreamChannel*, int) noexcept
{
    DestructorGuard dg(this);
    cnx_error_cb_(this);
}

void
Connection::onSocksTargetConnectResult(
    Socks5Connector* connector,
    Socks5ConnectorObserver::ConnectResult result) noexcept
{
    switch (result) {
    case Socks5ConnectorObserver::ConnectResult::OK: {
        CHECK_EQ(state_, State::CONNECTING);
        CHECK_EQ(socks_connector_.get(), connector);

        // get back our transport
        CHECK(!transport_);
        StreamChannel::UniquePtr tmp = socks_connector_->release_transport();
        transport_.reset((TCPChannel*)tmp.release());
        CHECK_NOTNULL(transport_);
        socks_connector_.reset();

        vlogself(2) << "connected to target (thru socks proxy)";
        state_ = State::CONNECTED;

        // need to set ourselves as observer again because
        // socks5connector overtook us
        transport_->set_observer(this);

        _maybe_send();

        break;
    }

    case Socks5ConnectorObserver::ConnectResult::ERR_FAIL:
    case Socks5ConnectorObserver::ConnectResult::ERR_FAIL_TRANSPORT_EOF:
    case Socks5ConnectorObserver::ConnectResult::ERR_FAIL_TRANSPORT_ERROR: {
        DestructorGuard dg(this);
        cnx_error_cb_(this);
        break;
    }

    default:
        logself(FATAL) << "invalid result";
        break;
    }
}

void
Connection::onConnected(StreamChannel* ch) noexcept
{
    if (state_ == State::PROXY_CONNECTING) {
        vlogself(2) << "now connected to the PROXY";

        CHECK_EQ(transport_.get(), ch);
        state_ = State::PROXY_CONNECTED;

        const in_addr_t& addr = (ssp_addr_) ? ssp_addr_ : addr_;
        const uint16_t port = (ssp_addr_) ? ssp_port_ : port_;
        CHECK_NE(addr, 0);
        CHECK_NE(port, 0);

        vlogself(2) << "now tell proxy to connect to target";
        CHECK(!socks_connector_);
        socks_connector_.reset(
            new Socks5Connector(std::move(transport_), addr, port));
        auto rv = socks_connector_->start_connecting(this);
        CHECK(!rv);
        state_ = State::CONNECTING;
    }
    else if (state_ == State::CONNECTING) {
        vlogself(2) << "connected to target";
        state_ = State::CONNECTED;
        _maybe_send();
    }
    else {
        logself(FATAL) << "invalid state";
    }
}

void
Connection::onConnectError(StreamChannel* ch, int errorcode) noexcept
{
    LOG(FATAL) << "to be implemented";
}

void
Connection::onConnectTimeout(myio::StreamChannel*) noexcept
{
    LOG(FATAL) << "to be implemented";
}

int
Connection::initiate_connection()
{
    vlogself(2) << "begin";

    int rv = 0;

    vlogself(2) << "socks5 " << socks5_addr_ << ":" << socks5_port_;

    if (state_ == State::DISCONNECTED && socks5_addr_ && socks5_port_) {
        vlogself(2) << "first, connect to socks proxy";

        CHECK(!transport_);
        transport_.reset(new TCPChannel(evbase_, socks5_addr_, socks5_port_, this));
        CHECK_NOTNULL(transport_);

        rv = transport_->start_connecting(this);
        CHECK_EQ(rv, 0);

        vlogself(2) <<  "transition to state PROXY_CONNECTING";
        state_ = State::PROXY_CONNECTING;
    }
    else if (state_ == State::DISCONNECTED) {
        const in_addr_t addr = (addr_) ? addr_ : ssp_addr_;
        const uint16_t port = (port_) ? port_ : ssp_port_;

        CHECK_NE(addr, 0);
        CHECK_NE(port, 0);

        CHECK(!transport_);
        transport_.reset(new TCPChannel(evbase_, addr, port, this));
        CHECK_NOTNULL(transport_);

        rv = transport_->start_connecting(this);
        CHECK_EQ(rv, 0);

        vlogself(2) << "transition to state CONNECTING";
        state_ = State::CONNECTING;
    }
    else {
        logself(FATAL) << "invalid state: " << common::as_integer(state_);
    }

    return rv;
}

void
Connection::_maybe_send()
{
    if (use_spdy_) {
        spdylay_session* session = spdysess_.get();
        auto rv = spdylay_session_send(session);
        if (rv) {
            disconnect();
            if (SPDYLAY_ERR_EOF == rv) {
                vlogself(1) << "remote peer closed";
            } else {
                logself(WARNING) << "spdylay_session_send() returned \""
                             << spdylay_strerror(rv) << "\"";
            }
            return;
        }

        if(spdylay_session_want_read(session) == 0 &&
           spdylay_session_want_write(session) == 0)
        {
            CHECK(0);
        }
    } else {
        _maybe_http_write_to_transport();
    }
    return;
}

ssize_t
Connection::spdylay_send_cb(spdylay_session *session, const uint8_t *data,
                            size_t length, int flags)
{
    vlogself(2) << "begin, length= " << length;
    CHECK_EQ(session, spdysess_.get());

    ssize_t retval = SPDYLAY_ERR_CALLBACK_FAILURE;

    const auto rv = transport_->write(data, length);
    if (rv == 0) {
        cumulative_num_sent_bytes_ += length;
        vlogself(2) << "able to write " << length << " bytes to transport";
        retval = length;
    }
    else if (rv == -1) {
        logself(WARNING) << "transport didn't accept our write";
        retval = SPDYLAY_ERR_CALLBACK_FAILURE;
    } else {
        logself(FATAL) << "invalid rv= " << rv;
    }
 
    vlogself(2) << "returning " << retval;
    return retval;
}


ssize_t
Connection::spdylay_recv_cb(spdylay_session *session, uint8_t *buf,
                            size_t length, int flags)
{
    vlogself(2) << "begin, length="<< length;
    CHECK_EQ(session, spdysess_.get());

    ssize_t retval = SPDYLAY_ERR_CALLBACK_FAILURE;

    const auto numread = transport_->read(buf, length);

    if (numread == 0) {
        vlogself(2) << "no data is currently available for reading";
        retval = transport_->is_closed()
                 ? SPDYLAY_ERR_EOF
                 : SPDYLAY_ERR_WOULDBLOCK;
    }
    else if (numread > 0) {
        vlogself(2) << "able to read " << numread << " bytes";
        retval = numread;
        if (0 == cumulative_num_recv_bytes_
            && cnx_first_recv_byte_cb_)
        {
            cnx_first_recv_byte_cb_(this);
        }
        cumulative_num_recv_bytes_ += numread;
    }
    else {
        logself(WARNING) << "transport::read() returns: " << numread;
    }

    vlogself(2) << "returning " << retval;
    return retval;
}

void
Connection::spdylay_on_data_recv_cb(spdylay_session *session,
                                    uint8_t flags, int32_t stream_id,
                                    int32_t len)
{
    CHECK_EQ(session, spdysess_.get());
    const int32_t sid = stream_id;
    vlogself(2) << "begin, sid " << sid << ", len " << len << ", cnx " << objId();
    if ((flags & SPDYLAY_DATA_FLAG_FIN) != 0) {

        DestructorGuard dg(this);

        vlogself(2) << "last data frame of resource";

        if (inSet(psids_, sid)) {
            notify_pushed_body_done_(sid, this, cb_data_);
            return;
        }

        CHECK(inMap(sid2req_, sid));
        Request* req = sid2req_[sid];
        req->notify_rsp_done();
    }
    vlogself(2) << "done";
}

void
Connection::spdylay_on_data_chunk_recv_cb(spdylay_session *session,
                                          uint8_t flags, int32_t stream_id,
                                          const uint8_t *data, size_t len)
{
    CHECK(session == spdysess_.get());
    const int32_t sid = stream_id;
    // logself(DEBUG, "begin, sid %d, len %d, cnx %u",
    //         sid, len, instNum_);

    DestructorGuard dg(this);

    if (inSet(psids_, sid)) {
        notify_pushed_body_data_(sid, data, len, this, cb_data_);
        return;
    }

    CHECK(inMap(sid2req_, sid));
    Request* req = sid2req_[sid];
    req->notify_rsp_body_data(data, len);

    // logself(DEBUG, "done");
}

void
Connection::handle_server_push_ctrl_recv(spdylay_frame *frame)
{
    // must include Associated-To-Stream-ID
    const int32_t pushsid = frame->syn_stream.stream_id;
    const int32_t assoc_sid = frame->syn_stream.assoc_stream_id;

    // logself(DEBUG, "begin, server-pushed stream %d with assoc id %d",
    //         pushsid, assoc_sid);

    CHECK (assoc_sid != 0);

    char **nv = frame->syn_stream.nv;
    const char *content_length = 0;
    const char *pushedurl = nullptr;
    unsigned int code = 0;
    //const char *content_length = 0;
    ssize_t contentlen = -1;

    for(size_t i = 0; nv[i]; i += 2) {
        if(strcmp(nv[i], ":status") == 0) {
            code = strtoul(nv[i+1], 0, 10);
            CHECK(code == 200);
        } else if(nv[i][0] != ':') {
            if(strcasecmp(nv[i], "content-length") == 0) {
                contentlen = strtoul(nv[i+1], 0, 10);
            }
        } else if(strcmp(nv[i], ":pushedurl") == 0) {
            pushedurl = nv[i+1];
        }
    }

    // logself(DEBUG, "pushed url: [%s], contentlen %d", pushedurl, contentlen);

    //sid2psids_[assoc_sid].push_back(pushsid);
    psids_.insert(pushsid);

    notify_pushed_meta_(
        pushsid, pushedurl, contentlen, (const char**)nv, this, cb_data_);

    // logself(DEBUG, "done");
}
    
void
Connection::spdylay_on_ctrl_recv_cb(spdylay_session *session,
                                    spdylay_frame_type type,
                                    spdylay_frame *frame)
{
    // logself(DEBUG, "begin");
    CHECK(session == spdysess_.get());
    switch(type) {
    case SPDYLAY_SYN_STREAM:
        handle_server_push_ctrl_recv(frame);
        break;
    case SPDYLAY_RST_STREAM:
        CHECK(0);
        break;
    case SPDYLAY_SYN_REPLY: {
        const int32_t sid = frame->syn_reply.stream_id;
        CHECK(inMap(sid2req_, sid));
        Request* req = sid2req_[sid];
        CHECK(req);
        // logself(DEBUG,
        //         "SYN REPLY sid: %d, cnx: %u, req: %u",
        //         sid, instNum_, req->instNum_);

        char **nv = frame->syn_reply.nv;
        const char *status = 0;
        const char *version = 0;
        const char *content_length = 0;
        unsigned int code = 0;
        for(size_t i = 0; nv[i]; i += 2) {
            // logself(DEBUG, "name-value pair: %s: %s", nv[i], nv[i+1]);
            if(strcmp(nv[i], ":status") == 0) {
                code = strtoul(nv[i+1], 0, 10);
                CHECK(code == 200);
                status = nv[i+1];
            } else if(strcmp(nv[i], ":version") == 0) {
                version = nv[i+1];
            } else if(nv[i][0] != ':') {
                // if(strcmp(nv[i], "content-length") == 0) {
                //     content_length = nv[i+1];
                //     downstream->content_length_ = strtoul(content_length, 0, 10);
                // }
                //downstream->add_response_header(nv[i], nv[i+1]);
            }
        }
        if(!status || !version) {
            CHECK(0);
            return;
        }
        DestructorGuard dg(this);
        // logself(DEBUG, "notifying of response meta");
        req->notify_rsp_meta(code, nv);
    }
    default:
        break;
    }
    // logself(DEBUG, "done");
}

void
Connection::spdylay_before_ctrl_send_cb(spdylay_session *session,
                                        spdylay_frame_type type,
                                        spdylay_frame *frame)
{
    // logself(DEBUG, "begin");
    CHECK(session == spdysess_.get());
    if(type == SPDYLAY_SYN_STREAM) {
        const int32_t sid = frame->syn_stream.stream_id;
        Request *req = reinterpret_cast<Request*>(
            spdylay_session_get_stream_user_data(session, sid));
        CHECK(req);
        // logself(DEBUG, "new SYN sid: %d, cnx: %u, req: %u",
        //         sid, instNum_, req->instNum_);
        sid2req_[frame->syn_stream.stream_id] = req;
        req->dump_debug();
        DestructorGuard dg(this);
        req->notify_req_about_to_send();
    }
    // logself(DEBUG, "done");
}

ssize_t
Connection::s_spdylay_send_cb(spdylay_session *session, const uint8_t *data,
                              size_t length, int flags, void *user_data)
{
    Connection *conn = (Connection*)(user_data);
    return conn->spdylay_send_cb(session, data, length, flags);;
}

ssize_t
Connection::s_spdylay_recv_cb(spdylay_session *session, uint8_t *buf, size_t length,
                            int flags, void *user_data)
{
    Connection *conn = (Connection*)(user_data);
    return conn->spdylay_recv_cb(session, buf, length, flags);
}

void
Connection::s_spdylay_on_data_recv_cb(spdylay_session *session,
                                    uint8_t flags, int32_t stream_id,
                                    int32_t len, void *user_data)
{
    Connection *conn = (Connection*)(user_data);
    conn->spdylay_on_data_recv_cb(session, flags, stream_id, len);
}

void
Connection::s_spdylay_on_data_chunk_recv_cb(spdylay_session *session,
                                            uint8_t flags, int32_t stream_id,
                                            const uint8_t *data, size_t len,
                                            void *user_data)
{
    Connection *conn = (Connection*)(user_data);
    conn->spdylay_on_data_chunk_recv_cb(session, flags, stream_id,
                                        data, len);
}

void
Connection::s_spdylay_on_ctrl_recv_cb(spdylay_session *session, spdylay_frame_type type,
                                    spdylay_frame *frame, void *user_data)
{
    Connection *conn = (Connection*)(user_data);
    conn->spdylay_on_ctrl_recv_cb(session, type, frame);
}

void
Connection::s_spdylay_before_ctrl_send_cb(spdylay_session *session,
                                          spdylay_frame_type type,
                                          spdylay_frame *frame,
                                          void *user_data)
{
    Connection *conn = (Connection*)(user_data);
    conn->spdylay_before_ctrl_send_cb(session, type, frame);
}

void
Connection::_setup_spdylay_session()
{
    spdylay_session_callbacks callbacks;
    bzero(&callbacks, sizeof callbacks);
    callbacks.send_callback = s_spdylay_send_cb;
    callbacks.recv_callback = s_spdylay_recv_cb;
    callbacks.on_ctrl_recv_callback = s_spdylay_on_ctrl_recv_cb;
    callbacks.on_data_chunk_recv_callback = s_spdylay_on_data_chunk_recv_cb;
    callbacks.on_data_recv_callback = s_spdylay_on_data_recv_cb;
    /* callbacks.on_stream_close_callback = spdylay_on_stream_close_cb; */
    callbacks.before_ctrl_send_callback = s_spdylay_before_ctrl_send_cb;
    /* callbacks.on_ctrl_not_send_callback = spdylay_on_ctrl_not_send_cb; */

    spdylay_session *session = nullptr;

    // version 3 just adds flow control, compared to version 2
    auto r = spdylay_session_client_new(&session, 2, &callbacks, this);
    CHECK_EQ(r, 0);

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

    spdysess_.reset(session);
    session = nullptr;

    return;
}
