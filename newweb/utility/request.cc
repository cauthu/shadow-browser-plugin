#include <glib.h>
#include <glib/gprintf.h>
#include "myassert.h"
#include <time.h>

#include "request.hpp"
#include "common.hpp"

using std::vector;
using std::pair;
using std::string;

#ifdef ENABLE_MY_LOG_MACROS
/* "inst" stands for instance, as in, instance of a class */
#define loginst(level, inst, fmt, ...)                                  \
    do {                                                                \
        logfn(SHADOW_LOG_LEVEL_##level, __func__, "(ln %d, req= %d): " fmt,  \
              __LINE__, (inst)->instNum_, ##__VA_ARGS__);               \
    } while (0)

/* like loginst, but shortcut "this" as instance */
#define logself(level, fmt, ...)                                        \
    do {                                                                \
        logfn(SHADOW_LOG_LEVEL_##level, __func__, "(ln %d, req= %d): " fmt,  \
              __LINE__, (this)->instNum_, ##__VA_ARGS__);               \
    } while (0)

#else
/* no-ops */
#define loginst(level, inst, fmt, ...)

#define logself(level, fmt, ...)

#endif

Request::Request(
    const string& host, const uint16_t& port,
    const uint16_t& req_total_size,
    const uint16_t& resp_headers_size, const uint32_t& resp_body_size,
    RequestAboutToSendCb req_about_to_send_cb,
    ResponseMetaCb rsp_meta_cb, ResponseBodyDataCb rsp_body_data_cb,
    ResponseBodyDoneCb rsp_body_done_cb
    )
    : host_(host), port_(port)
    , req_total_size_(req_total_size)
    , resp_headers_size_(resp_headers_size), resp_body_size_(resp_body_size)
    , req_about_to_send_cb_(req_about_to_send_cb)
    , rsp_meta_cb_(rsp_meta_cb), rsp_body_data_cb_(rsp_body_data_cb)
    , rsp_body_done_cb_(rsp_body_done_cb)
    , conn(NULL), num_retries_(0)
{
    loginst(DEBUG, this, "a new request url [%s]", url.c_str());
    myassert(host_.length() > 0);
}

void
Request::dump_debug() const
{
    logDEBUG("dumping request to log:");
    logDEBUG("  host: %s", host_.c_str());
}
