
#include "request.hpp"
#include "easylogging++.h"

using std::vector;
using std::pair;
using std::string;


/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) << "req= " << (inst)->objId() << " "
#define vlogself(level) vloginst(level, this)

#define loginst(level, inst) LOG(level) << "req= " << (inst)->objId() << " "
#define logself(level) loginst(level, this)


Request::Request(
    const string& host, const uint16_t& port,
    const size_t& req_total_size,
    const size_t& resp_meta_size, const size_t& resp_body_size,
    RequestAboutToSendCb req_about_to_send_cb,
    ResponseMetaCb rsp_meta_cb, ResponseBodyDataCb rsp_body_data_cb,
    ResponseDoneCb rsp_done_cb
    )
    : host_(host), port_(port)
    , req_total_size_(req_total_size)
    , exp_resp_meta_size_(resp_meta_size), exp_resp_body_size_(resp_body_size)
    , req_about_to_send_cb_(req_about_to_send_cb)
    , rsp_meta_cb_(rsp_meta_cb), rsp_body_data_cb_(rsp_body_data_cb)
    , rsp_done_cb_(rsp_done_cb)
    , conn(NULL), num_retries_(0)
    , actual_resp_body_size_(0)
{
    vlogself(2) << "a new request ";
    CHECK_GT(host_.length(), 0);
}

void
Request::dump_debug() const
{
    vlogself(2) << "dumping request to log:";
    vlogself(2) << "  host: " << host_;
}
