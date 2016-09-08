/* handles a connection with the client
 */


#include <strings.h>

#include "handler.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"

#include <errno.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <boost/lexical_cast.hpp>

#ifdef __cplusplus /* If this is a C++ compiler, use C linkage */
extern "C" {
#endif

#include "../utility/http_parse.h"

#ifdef __cplusplus /* If this is a C++ compiler, end C linkage */
}
#endif

using std::string;
using boost::lexical_cast;
using myio::StreamChannel;


/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) << "hndlr= " << (inst)->objId() << " "
#define vlogself(level) vloginst(level, this)

#define loginst(level, inst) LOG(level) << "hndlr= " << (inst)->objId() << " "
#define logself(level) loginst(level, this)


Handler::Handler(StreamChannel::UniquePtr channel,
                 HandlerObserver* observer)
    : channel_(std::move(channel))
    , observer_(observer)
    , http_req_state_(HTTPReqState::HTTP_REQ_STATE_REQ_LINE)
    , remaining_req_body_length_(0)
{
    CHECK_NOTNULL(observer_);
    bzero(&current_req_, sizeof current_req_);
}

void
Handler::_maybe_consume_input()
{
    vlogself(2) << "begin";

    char *line = nullptr;
    bool keep_consuming = true;

    // don't free this inbuf
    struct evbuffer *inbuf = channel_->get_input_evbuf();
    CHECK_NOTNULL(inbuf);

    vlogself(2) << "num bytes available in inbuf: " << evbuffer_get_length(inbuf);

    if (evbuffer_get_length(inbuf) == 0) {
        vlogself(2) << "no input available";
        return;
    }

    do {
        switch (http_req_state_) {
        case HTTPReqState::HTTP_REQ_STATE_REQ_LINE: {
            CHECK_EQ(current_req_.active, 0);

            /* readln() does drain the buffer */
            line = evbuffer_readln(
                inbuf, nullptr, EVBUFFER_EOL_CRLF_STRICT);
            if (line) {
                vlogself(2) << "got request line: [" << line << "]";

                const auto line_len = strlen(line);
                CHECK_GT(line_len, 5);
                CHECK_LT(line_len, 63);

                /* get the (relative) path of the request */
                if (strncasecmp(line, "GET ", 4)) {
                    logself(FATAL) << "bad request line: [" << line << "]";
                }

                char* relpath = line + 4;
                char* relpath_end = strcasestr(relpath, " ");
                CHECK_NOTNULL(relpath_end);
                // make sure we're not out of bound
                CHECK_LT((relpath_end - line), line_len);
                *relpath_end = '\0';

                if (strcmp(relpath, common::http::request_path)) {
                    logself(FATAL) << "URI path [" << relpath << "] not expected";
                }

                current_req_.active = 1;

                http_req_state_ = HTTPReqState::HTTP_REQ_STATE_HEADERS;
                free(line);
                line = nullptr;
            } else {
                // not enough input data to find whole request line
                keep_consuming = false;
            }
            break;
        }

        case HTTPReqState::HTTP_REQ_STATE_HEADERS: {
            while (nullptr != (line = evbuffer_readln(
                                   inbuf, nullptr, EVBUFFER_EOL_CRLF_STRICT)))
            {
                if (line[0] == '\0') {
                    vlogself(2) << "no more hdrs";
                    free(line);
                    line = nullptr;

                    const RequestInfo& reqinfo = current_req_;
                    vlogself(2) << "req: resp_headers_size: " << reqinfo.resp_headers_size
                            << ", resp_body_size: " << reqinfo.resp_body_size
                            << ", content_length: " << remaining_req_body_length_;

                    if (remaining_req_body_length_) {
                        // there's a req body we need to consume
                        http_req_state_ = HTTPReqState::HTTP_REQ_STATE_BODY;
                    } else {
                        // no req body, so go serve response
                        _serve_response();
                    }
                }
                else {
                    vlogself(2) << "whole req hdr line: [" << line << "]";

                    const auto line_len = strlen(line);
                    CHECK_LT(line_len, 127); // arbitrary value

                    char *tmp = strchr(line, ':');
                    CHECK_NOTNULL(tmp);
                    // check bounds
                    CHECK_LE((tmp - line), (line_len - 3));

                    *tmp = '\0'; /* colon becomes NULL */
                    ++tmp;
                    *tmp = '\0'; /* blank space becomes NULL */
                    ++tmp;

                    const auto name_str = line;
                    const auto value_str = tmp;

                    RequestInfo& reqinfo = current_req_;

                    struct {
                        const char* hdr_name;
                        size_t* hdr_value_ptr;
                    } hdrs_to_parse[] = {
                        {
                            common::http::resp_headers_size_name,
                            &(reqinfo.resp_headers_size),
                        },
                        {
                            common::http::resp_body_size_name,
                            &(reqinfo.resp_body_size),
                        },
                        {
                            common::http::content_length_name,
                            &remaining_req_body_length_,
                        }
                    };

                    auto matched = false;
                    for (auto hdr_to_parse : hdrs_to_parse) {
                        if (!strcmp(name_str, hdr_to_parse.hdr_name)) {
                            try {
                                *(hdr_to_parse.hdr_value_ptr) =
                                    lexical_cast<size_t>(value_str);
                                matched = true;
                            } catch (const boost::bad_lexical_cast&) {
                                logself(FATAL) << "bad header value: " << value_str;
                            }
                            break;
                        }
                    }

                    CHECK(matched) << "unknown header name: [" << name_str << "]";

                    free(line);
                    line = nullptr;
                }
            } // end while readline for headers

            if (http_req_state_ == HTTPReqState::HTTP_REQ_STATE_HEADERS) {
                // if we're still trying to get more headers, that
                // means there was not enough input
                keep_consuming = false;
            }

            break;
        }

        case HTTPReqState::HTTP_REQ_STATE_BODY: {
            CHECK_GT(remaining_req_body_length_, 0);
            vlogself(2) << "drain body bytes that might already in input buf";
            const auto buflen = evbuffer_get_length(inbuf);
            const auto drain_len = std::min(buflen, remaining_req_body_length_);
            vlogself(2) << buflen << ", " << remaining_req_body_length_ << ", "
                        << drain_len;
            auto rv = evbuffer_drain(inbuf, drain_len);
            CHECK_EQ(rv, 0);
            remaining_req_body_length_ -= drain_len;

            if (remaining_req_body_length_ > 0) {
                CHECK_EQ(drain_len, buflen);
                CHECK_EQ(evbuffer_get_length(inbuf), 0);
                keep_consuming = false;
                vlogself(2) << "tell channel to drop "
                        << remaining_req_body_length_ << " future bytes";
                channel_->drop_future_input(
                    this, remaining_req_body_length_);
            } else {
                // have fully finished with req body, so go serve
                _serve_response();
            }
            break;
        }

        default:
            logself(FATAL) << "invalid state: " << common::as_integer(http_req_state_);
            break;
        }

    } while (keep_consuming && evbuffer_get_length(inbuf) > 0);

    if (line) {
        free(line);
    }
    vlogself(2) << "done";
    return;
}

void
Handler::_serve_response()
{
    CHECK_EQ(remaining_req_body_length_, 0);
    CHECK_EQ(current_req_.active, 1);

    std::unique_ptr<struct evbuffer, void(*)(struct evbuffer*)> buf(
        evbuffer_new(), evbuffer_free);
    CHECK_NOTNULL(buf.get());

    auto rv = evbuffer_add_printf(
        buf.get(),
        "HTTP/1.1 200 OK\r\n"
        "%s: %ld\r\n"
        "%s: ",
        common::http::content_length_name, current_req_.resp_body_size,
        common::http::dummy_name);
    CHECK_GT(rv, 0);

    // tell channel to write buf
    rv = channel_->write_buffer(buf.get());
    CHECK_EQ(rv, 0);
    CHECK_EQ(evbuffer_get_length(buf.get()), 0);

    vlogself(2) << "tell channel to write "
                << current_req_.resp_headers_size << " dummy header bytes";
    CHECK_GT(current_req_.resp_headers_size, 0);

    rv = channel_->write_dummy(current_req_.resp_headers_size);
    CHECK_EQ(rv, 0);

    // end the dummy header and overall resp meta info
    rv = evbuffer_add_printf(buf.get(), "\r\n\r\n");
    CHECK_GT(rv, 0);

    rv = channel_->write_buffer(buf.get());
    CHECK_EQ(rv, 0);
    CHECK_EQ(evbuffer_get_length(buf.get()), 0);

    if (current_req_.resp_body_size > 0) {
        vlogself(2) << "tell channel to write "
                    << current_req_.resp_body_size << " dummy BODY bytes";

        rv = channel_->write_dummy(current_req_.resp_body_size);
        CHECK_EQ(rv, 0);
    }

    vlogself(2) << "reset state to get next request";

    bzero(&current_req_, sizeof current_req_);
    http_req_state_ = HTTPReqState::HTTP_REQ_STATE_REQ_LINE;
}

void
Handler::onNewReadDataAvailable(StreamChannel* channel) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    _maybe_consume_input();
}

void
Handler::onEOF(StreamChannel* channel) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    DestructorGuard dg(this);
    observer_->onHandlerDone(this);
}

void
Handler::onError(StreamChannel* channel, int errorcode) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    logself(WARNING) << "channel closed on error: " << errorcode;
    DestructorGuard dg(this);
    observer_->onHandlerDone(this);
}

void
Handler::onCompleteInputDrop(StreamChannel* channel, size_t len) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    CHECK_EQ(http_req_state_, HTTPReqState::HTTP_REQ_STATE_BODY);
    CHECK_EQ(remaining_req_body_length_, len);

    remaining_req_body_length_ -= len;
    CHECK_EQ(remaining_req_body_length_, 0);

    _serve_response();
}

Handler::~Handler()
{
    logself(INFO) << "handler " << objId() << " destructor";
}
