
#include "request.hpp"
#include "../easylogging++.h"

using std::vector;
using std::pair;
using std::string;


#define _LOG_PREFIX(inst) << "req= " << (inst)->objId() << ", res:" << (inst)->webkit_resInstNum_ << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


namespace http
{

Request::Request(
    const uint32_t& webkit_resInstNum,
    const string& host, const uint16_t& port,
    const size_t& req_total_size,
    const size_t& resp_meta_size, const size_t& resp_body_size,
    RequestAboutToSendCb req_about_to_send_cb,
    ResponseMetaCb rsp_meta_cb, ResponseBodyDataCb rsp_body_data_cb,
    ResponseDoneCb rsp_done_cb
    )
    : webkit_resInstNum_(webkit_resInstNum)
    , host_(host), port_(port)
    , req_total_size_(req_total_size)
    , exp_resp_meta_size_(resp_meta_size), exp_resp_body_size_(resp_body_size)
    , req_about_to_send_cb_(req_about_to_send_cb)
    , rsp_meta_cb_(rsp_meta_cb), rsp_body_data_cb_(rsp_body_data_cb)
    , rsp_done_cb_(rsp_done_cb)
    , conn(NULL), num_retries_(0)
    , actual_resp_body_size_(0)
    , first_byte_recv_time_(0)
{
    vlogself(2) << "a new request, res:" << webkit_resInstNum;
    CHECK_GT(host_.length(), 0);
}

Request::~Request()
{
    vlogself(3) << "destructing";
}

void
Request::dump_debug() const
{
    vlogself(2) << "dumping request to log:";
    vlogself(2) << "  host: " << host_;
}

} // end namespace http
