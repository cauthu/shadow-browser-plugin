#ifndef SHD_REQUEST_HPP
#define SHD_REQUEST_HPP

#include <string>
#include <vector>
#include <boost/function.hpp>

#include "object.hpp"
#include "easylogging++.h"

class Connection;
class Request;


typedef boost::function<void(const int status, char **headers, Request* req)> ResponseMetaCb;
/* tell the user of a block of response body data */
typedef boost::function<void(const uint8_t *data, const size_t& len, Request* req)> ResponseBodyDataCb;

/*
 * tell the user THE WHOLE response is done, whether or not there is a
 * response body, i.e., if there is response no body, then this will
 * be called after done receiving the headers
 *
 */
typedef boost::function<void(Request *req)> ResponseDoneCb;

/* tell the user the request is about to be sent into the network */
typedef boost::function<void(Request *req)> RequestAboutToSendCb;


class Request : public Object
{
public:
    Request(const std::string& host, const uint16_t& port,
            const size_t& req_total_size,
            const size_t& resp_headers_size, const size_t& resp_body_size,

            RequestAboutToSendCb req_about_to_send_cb,
            ResponseMetaCb rsp_meta_cb,
            ResponseBodyDataCb rsp_body_data_cb,
            ResponseDoneCb rsp_done_cb
        );

    // for response
    void notify_rsp_meta(const int status, char ** headers) {
        DestructorGuard dg(this);
        rsp_meta_cb_(status, headers, this);
    }
    void notify_rsp_body_data(const uint8_t *data, const size_t& len) {
        actual_resp_body_size_ += len;
        DestructorGuard dg(this);
        rsp_body_data_cb_(data, len, this);
    }
    void notify_rsp_done() {
        CHECK_EQ(exp_resp_body_size_, actual_resp_body_size_);
        DestructorGuard dg(this);
        rsp_done_cb_(this);
    }
    void notify_req_about_to_send() {
        if (req_about_to_send_cb_) {
            DestructorGuard dg(this);
            req_about_to_send_cb_(this);
        }
    }
    const size_t& actual_resp_body_size() const { return actual_resp_body_size_; }
    void dump_debug() const;
    const std::vector<std::pair<std::string, std::string> > & get_headers() const {
        return headers_;
    }

    int32_t get_num_retries() const { return num_retries_; }
    void increment_num_retries() { ++num_retries_; }

    const size_t& req_total_size() const { return req_total_size_; }
    const size_t& exp_resp_headers_size() const { return exp_resp_headers_size_; }
    const size_t& exp_resp_body_size() const { return exp_resp_body_size_; }

    // these are const, so ok to expose
    const std::string host_; /* for host header */
    const uint16_t port_;
    /* the cnx handling this req. currently Request class is not doing
     * anything with this pointer; it's here only for convenience of
     * other code */
    Connection* conn;

private:

    virtual ~Request() = default;

    std::vector<std::pair<std::string, std::string> > headers_;

    RequestAboutToSendCb req_about_to_send_cb_;
    ResponseMetaCb rsp_meta_cb_;
    ResponseBodyDataCb rsp_body_data_cb_;
    ResponseDoneCb rsp_done_cb_;

    uint8_t num_retries_;

    const size_t req_total_size_;

    // these are amounts we want the server to send to us
    const size_t exp_resp_headers_size_;
    const size_t exp_resp_body_size_;

    // this is what we see
    size_t actual_resp_body_size_;
};

#endif /* SHD_REQUEST_HPP */
