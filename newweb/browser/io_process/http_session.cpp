
#include <boost/bind.hpp>

#include "http_session.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"


using std::shared_ptr;
using std::string;



#define _LOG_PREFIX(inst) << "hsess= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


HttpNetworkSession::HttpNetworkSession(struct event_base* evbase,
                                       IPCServer* ipcserver,
                                       uint32_t routing_id,
                                       const NetConfig* netconf)
    : evbase_(evbase)
    , ipcserver_(ipcserver), routing_id_(routing_id)
    , netconf_(netconf)
{
    connman_.reset(
        new ConnectionManager(
            evbase_,
            netconf->socks5_addr(), netconf->socks5_port(),
            boost::bind(&HttpNetworkSession::_response_done_cb, this,
                        _1, false)));
    CHECK_NOTNULL(connman_.get());
}

void
HttpNetworkSession::handle_Fetch(const msgs::FetchMsg* msg)
{
    const string hostname(msg->host()->c_str());
    const auto port = msg->port();

    vlogself(2) << "got fetch req: [" << hostname << ":" << port
                << "] " << msg->req_total_size()
                << ", " << msg->resp_headers_size()
                << ", " << msg->resp_body_size();

    shared_ptr<Request> req(
        new Request(
            hostname, port, msg->req_total_size(),
            msg->resp_headers_size(), msg->resp_body_size(),
            NULL,
            boost::bind(
                &HttpNetworkSession::_response_meta_cb, this, _1, _2, _3),
            boost::bind(
                &HttpNetworkSession::_response_body_data_cb, this, _1, _2, _3),
            boost::bind(
                &HttpNetworkSession::_response_done_cb, this, _1, true)
            ),
        [](Request* req) { req->destroy(); }
        );
    connman_->submit_request(req.get());

    const auto ret = pending_requests_.insert(make_pair(req->objId(), req));
    CHECK(ret.second);
}

void
HttpNetworkSession::_response_meta_cb(const int& status,
                                      char **headers,
                                      Request* req)
{
    dvlogself(2) << "begin";

    // don't care to do anything other than making sure it's 200
    CHECK_EQ(status, 200);

    dvlogself(2) << "done";
}

void
HttpNetworkSession::_response_body_data_cb(
    const uint8_t *data, const size_t& len, Request* req)
{


}


void
HttpNetworkSession::_response_done_cb(Request* req, bool success)
{

}
