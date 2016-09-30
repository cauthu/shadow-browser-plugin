
#include <boost/bind.hpp>

#include "http_session.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"


using std::shared_ptr;
using std::string;
using std::make_pair;


#define _LOG_PREFIX(inst) << "hsess= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


using http::Request;
using http::ConnectionManager;


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
                        _1, false),
            8, 0));
    CHECK_NOTNULL(connman_.get());
}

void
HttpNetworkSession::handle_RequestResource(const int req_res_req_id,
                                           const char* host,
                                           const uint16_t port,
                                           const size_t req_total_size,
                                           const size_t resp_meta_size,
                                           const size_t resp_body_size)
{
    const string hostname(host);

    vlogself(2) << "got RequestResource: id " << req_res_req_id
                << " [" << hostname << ":" << port << "] "
                << ", " << req_total_size
                << ", " << resp_meta_size
                << ", " << resp_body_size;

    shared_ptr<Request> req(
        new Request(
            hostname, port, req_total_size,
            resp_meta_size, resp_body_size,
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

    PendingRequestInfo pri;
    pri.req_res_req_id = req_res_req_id;
    pri.req = req;
    const auto ret = pending_requests_.insert(make_pair(req->objId(), pri));
    CHECK(ret.second);

    vlogself(2) << "res req id: " << req_res_req_id
                << " linked up with request objId: " << req->objId();
}

void
HttpNetworkSession::_response_meta_cb(const int& status,
                                      char **headers,
                                      Request* req)
{
    const auto req_objId = req->objId();
    vlogself(2) << "begin, req objId: " << req_objId;

    CHECK_EQ(status, 200);

    if (!inMap(pending_requests_, req_objId)) {
        logself(FATAL) << "unknown request objId " << req_objId;
    }

    const auto req_res_req_id = pending_requests_[req_objId].req_res_req_id;

    // tell renderer
    ipcserver_->send_ReceivedResponse(routing_id_, req_res_req_id);

    vlogself(2) << "done";
}

void
HttpNetworkSession::_response_body_data_cb(
    const uint8_t *data, const size_t& len, Request* req)
{
    const auto req_objId = req->objId();
    vlogself(2) << "begin, req objId: " << req_objId
                << " len: " << len;

    CHECK_GT(len, 0);

    if (!inMap(pending_requests_, req_objId)) {
        logself(FATAL) << "unknown request objId " << req_objId;
    }

    const auto req_res_req_id = pending_requests_[req_objId].req_res_req_id;

    // tell renderer about the body data chunk (just its size)
    ipcserver_->send_DataReceived(routing_id_, req_res_req_id, len);

    vlogself(2) << "done";
}


void
HttpNetworkSession::_response_done_cb(Request* req, bool success)
{
    const auto req_objId = req->objId();
    vlogself(2) << "begin, req objId: " << req_objId;

    if (!inMap(pending_requests_, req_objId)) {
        logself(FATAL) << "unknown request objId " << req_objId;
    }

    const auto req_res_req_id = pending_requests_[req_objId].req_res_req_id;

    if (!success) {
        logself(WARNING) << "request objId " << req_objId << " failed";
    }

    // tell renderer we're done with the request
    ipcserver_->send_RequestComplete(routing_id_, req_res_req_id, success);

    vlogself(2) << "done";
}
