
#include <string>
#include <utility>
#include <boost/bind.hpp>

#include "connection_manager.hpp"
#include "../easylogging++.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

using std::string;
using std::pair;
using std::queue;
using std::list;
using std::map;
using std::make_pair;
using std::shared_ptr;
using std::make_shared;


#define _LOG_PREFIX(inst) << "cnxman= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)



namespace http
{

/***************************************************/

ConnectionManager::ConnectionManager(struct event_base *evbase,
                                     const in_addr_t& socks5_addr, 
                                     const in_port_t& socks5_port,
                                     RequestErrorCb request_error_cb,
                                     const uint8_t max_persist_cnx_per_srv,
                                     const uint8_t max_retries_per_resource)
    : evbase_(evbase)
    , socks5_addr_(socks5_addr), socks5_port_(socks5_port)
    , max_persist_cnx_per_srv_(max_persist_cnx_per_srv)
    , max_retries_per_resource_(max_retries_per_resource)

    , timestamp_recv_first_byte_(0)
    , totaltxbytes_(0), totalrxbytes_(0)

    , notify_req_error_(request_error_cb)
{
    CHECK(evbase_);
    CHECK(request_error_cb);
    CHECK(max_persist_cnx_per_srv > 0);

    CHECK_EQ(max_retries_per_resource_, 0)
        << "don't use retries for this project";
}

/***************************************************/

void
ConnectionManager::submit_request(Request *req)
{
    vlogself(2) << "begin, req= " << req->objId();

    const NetLoc netloc(req->host_, req->port_);

    vlogself(2) << "netloc: " << netloc.first << ":" << netloc.second;

    if (!inMap(servers_, netloc)) {
        servers_[netloc] = make_shared<Server>();
    }
    auto server = servers_[netloc];
    server->requests_.push_back(req);

    vlogself(2) << "server queue size " << server->requests_.size();

    shared_ptr<Connection> conn;
    auto& conns = server->connections_;

    // first, is there a connection with an empty queue
    for (auto& c : conns) {
        if (c->get_queue_size() == 0) {
            conn = c;
            vlogself(2) << "conn " << c->objId() << " has empty queue -> use it";
            goto done;
        }
    }

    vlogself(2) << "reaching here means no idle connection";
    CHECK(!conn); // make sure conn IS NULL

    vlogself(2) << "there are " << conns.size()<< " connections to this netloc";

    if (conns.size() < max_persist_cnx_per_srv_) {
        vlogself(2) << " --> create a new connection";
        CHECK(!conn);
        conn.reset(new Connection(
                       evbase_,
                       common::getaddr(netloc.first.c_str()), netloc.second,
                       socks5_addr_, socks5_port_,
                       0, 0,
                       boost::bind(&ConnectionManager::cnx_error_cb, this, _1, netloc),
                       boost::bind(&ConnectionManager::cnx_eof_cb, this, _1, netloc),
                       nullptr, nullptr, nullptr,
                       this,
                       false
                       ),
                   [=](Connection* c) { c->destroy(); });
        CHECK(conn);
        vlogself(2) << " ... with objId() "<< conn->objId();
        conn->set_request_done_cb(
            boost::bind(&ConnectionManager::cnx_request_done_cb, this, _1, _2, netloc));
        conn->set_first_recv_byte_cb(
            boost::bind(&ConnectionManager::cnx_first_recv_byte_cb, this, _1));
        conns.push_back(conn);
        goto done;
    } else {
        vlogself(2)<< "reached max persist cnx per srv -> do nothing now";
    }

done:
    if (conn) {
        CHECK(server->requests_.size() > 0);

        auto reqtosubmit = server->requests_.front();
        vlogself(2) << "submit request [" << reqtosubmit->objId() << "] on conn objId() "
                    << conn->objId();
        conn->submit_request(reqtosubmit);
        server->requests_.pop_front();
    }
    vlogself(2) << "done";
    return;
}

/***************************************************/

void
ConnectionManager::cnx_first_recv_byte_cb(Connection* conn)
{
    vlogself(2) << "begin";
    if (timestamp_recv_first_byte_ != 0) {
        vlogself(2) << "timestamp_recv_first_byte_ already set: "
                    << timestamp_recv_first_byte_ << " --> do nothing";
        return;
    }
    timestamp_recv_first_byte_ = common::gettimeofdayMs(nullptr);
    CHECK(timestamp_recv_first_byte_ > 0);
    vlogself(2) << "timestamp_recv_first_byte_: " << timestamp_recv_first_byte_;
    vlogself(2) << "done";
}

/***************************************************/

void
ConnectionManager::cnx_request_done_cb(Connection* conn,
                                       const Request* req,
                                       const NetLoc& netloc)
{
    vlogself(2) << "begin, req " <<  req->objId();

    // we don't free anything in here

    // see if there's a request waiting to be sent

    Request* reqtosubmit = nullptr;

    auto server = servers_[netloc];
    CHECK(server);

    auto& requests = server->requests_;
    vlogself(2) << requests.size() << " waiting requests";

    if (requests.empty()) {
        vlogself(2) << "  --> do nothing";
        goto done;
    }

    reqtosubmit = requests.front();
    vlogself(2) << "submit request [" << reqtosubmit->objId()
                << "] on conn objId() " << conn->objId();
    conn->submit_request(reqtosubmit);
    requests.pop_front();

done:
    vlogself(2) << "done";
    return;
}

/***************************************************/

void
ConnectionManager::cnx_error_cb(Connection* conn,
                                const NetLoc& netloc)
{
    logself(WARNING) << "connection error";
    handle_unusable_conn(conn, netloc);
}

/***************************************************/

void
ConnectionManager::cnx_eof_cb(Connection* conn,
                              const NetLoc& netloc)
{
    logself(WARNING) << "connection eof";
    handle_unusable_conn(conn, netloc);
}

/***************************************************/

void
ConnectionManager::handle_unusable_conn(Connection *conn,
                                        const NetLoc& netloc)
{
    vlogself(2) << "begin, cnx: "<< conn->objId();

    // we should mark any requests being handled by this connection as
    // error. for now, we don't attempt to request elsewhere.

    release_conn(conn, netloc);

    /* release_conn() only removes the conn from the list. it does not
     * yet delete the conn object. so we can still get its request
     * queues.
     *
     * retry_requests() honors the max_retries_per_resource_
     */
    retry_requests(conn->get_active_request_queue());

    vlogself(2) << "done";
}

/***************************************************/

bool
ConnectionManager::retry_requests(queue<Request*> requests)
{
    vlogself(2) << "begin, num reqs= " << requests.size();

    while (!requests.empty()) {
        auto req = requests.front();
        CHECK(req);
        vlogself(2) << "req objid " << req->objId();
        requests.pop();
        if (req->get_num_retries() == max_retries_per_resource_) {
            logself(WARNING) << "resource [" << req->objId() << "] has exhausted "
                             <<  unsigned(max_retries_per_resource_) << " retries";
            notify_req_error_(req);
            continue;
        }
        req->increment_num_retries();
        logself(INFO) <<
            "re-requesting resource ["<<req->objId()<<"] for the "<<req->get_num_retries()<<" time";
        if (req->actual_resp_body_size() > 0) {
            /* the request "body_size()" represents number of
             * contiguous bytes from 0 that we have received. so, we
             * can use that as the next first_byte_pos.
             */
            // req->set_first_byte_pos(req->get_body_size());
            // vlogself(2) << "set first_byte_pos to %d",
            //         req->get_first_byte_pos());
        }

        this->submit_request(req);
    }

    vlogself(2) << "done";
    return true;
}

/***************************************************/

void
ConnectionManager::get_total_bytes(size_t& tx, size_t& rx)
{
    vlogself(2) << "begin";

    tx = totaltxbytes_;
    rx = totalrxbytes_;

    // pair<NetLoc, Server*> kv_pair;
    for (const auto& kv_pair : servers_) {
        vlogself(2) << "server [" << kv_pair.first.first << "]:" << kv_pair.first.second;
        const auto server = kv_pair.second;
        for (auto& c : server->connections_) {
            tx += c->get_total_num_sent_bytes();
            rx += c->get_total_num_recv_bytes();
        }
    }

    vlogself(2) << "done";
}

/***************************************************/

void
ConnectionManager::reset()
{
    // we don't touch the Request* pointers.
    // pair<NetLoc, Server*> kv_pair;
    for (const auto& kv_pair: servers_) {
        vlogself(2) << "clearing server ["<< kv_pair.first.first <<"]:"
                    << kv_pair.first.second;
        const auto server = kv_pair.second;
        server->connections_.clear();
    }
    servers_.clear();

    timestamp_recv_first_byte_ = 0;
    totaltxbytes_ = totalrxbytes_ = 0;
}

/***************************************************/

void
ConnectionManager::release_conn(Connection *conn,
                                const NetLoc& netloc)
{
    vlogself(2) << "begin, releasing cnx "<< conn->objId();

    // remove it from active connections
    CHECK(inMap(servers_, netloc));
    auto& conns = servers_[netloc]->connections_;

    auto finditer = std::find_if(
        conns.begin(), conns.end(),
        [&](shared_ptr<Connection> const& p) { return p.get() == conn; });
    CHECK(finditer != conns.end());
    conns.erase(finditer);
    if (conns.size() == 0) {
        vlogself(2) << "list is now empty --> remove this list from map";
        servers_.erase(netloc);
    }

    totaltxbytes_ += conn->get_total_num_sent_bytes();
    totalrxbytes_ += conn->get_total_num_recv_bytes();

    vlogself(2) << "totaltxbytes_ " << totaltxbytes_ << ", totalrxbytes_ " << totalrxbytes_;

    vlogself(2) << "done";
}

} // end namespace http
