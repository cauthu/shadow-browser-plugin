
#include "connection_manager.hpp"

#include <string>
#include <utility>
#include <boost/bind.hpp>

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

#ifdef ENABLE_MY_LOG_MACROS
/* "inst" stands for instance, as in, instance of a class */
#define loginst(level, inst, fmt, ...)                                  \
    do {                                                                \
        logfn(SHADOW_LOG_LEVEL_##level, __func__, "(ln %d, cnxman= %d): " fmt, \
              __LINE__, (inst)->objId(), ##__VA_ARGS__);               \
    } while (0)

/* like loginst, but shortcut "this" as instance */
#define logself(level, fmt, ...)                                        \
    do {                                                                \
        logfn(SHADOW_LOG_LEVEL_##level, __func__, "(ln %d, cnxman= %d): " fmt, \
              __LINE__, (this)->objId(), ##__VA_ARGS__);               \
    } while (0)
#else

/* no op */
#define loginst(level, inst, fmt, ...)

/* no op */
#define logself(level, fmt, ...)

#endif

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
    myassert(evbase_);
    myassert(request_error_cb);
    myassert(max_persist_cnx_per_srv > 0);
}

/***************************************************/

void
ConnectionManager::submit_request(Request *req)
{
    logself(DEBUG, "begin, req url [%s]", req->url_.c_str());

    const NetLoc netloc(req->host_, req->port_);

    logself(DEBUG, "netloc: %s:%u", netloc.first.c_str(), netloc.second);

    if (!inMap(servers_, netloc)) {
        servers_[netloc] = make_shared<Server>();
    }
    auto server = servers_[netloc];
    server->requests_.push_back(req);

    logself(DEBUG, "server queue size %u", server->requests_.size());

    shared_ptr<Connection> conn;
    auto& conns = server->connections_;

    // first, is there a connection with an empty queue
    for (auto& c : conns) {
        if (c->get_queue_size() == 0) {
            conn = c;
            logself(DEBUG, "conn %d has empty queue -> use it", c->objId());
            goto done;
        }
    }

    logself(DEBUG, "reaching here means no idle connection");
    myassert(!conn); // make sure conn IS NULL

    logself(DEBUG, "there are %u connections to this netloc", conns.size());

    if (conns.size() < max_persist_cnx_per_srv_) {
        logself(DEBUG, " --> create a new connection");
        myassert(!conn);
        conn.reset(new Connection(
                       evbase_,
                       getaddr(netloc.first.c_str()), netloc.second,
                       socks5_addr_, socks5_port_,
                       0, 0,
                       boost::bind(&ConnectionManager::cnx_error_cb, this, _1, netloc),
                       boost::bind(&ConnectionManager::cnx_eof_cb, this, _1, netloc),
                       nullptr, nullptr, nullptr,
                       this,
                       false
                       ),
                   [=](Connection* c) { c->destroy(); });
        myassert(conn);
        logself(DEBUG, " ... with objId() %u", conn->objId());
        conn->set_request_done_cb(
            boost::bind(&ConnectionManager::cnx_request_done_cb, this, _1, _2, netloc));
        conn->set_first_recv_byte_cb(
            boost::bind(&ConnectionManager::cnx_first_recv_byte_cb, this, _1));
        conns.push_back(conn);
        goto done;
    } else {
        logself(DEBUG,
                "reached max persist cnx per srv -> do nothing now");
    }

done:
    if (conn) {
        myassert(server->requests_.size() > 0);

        auto reqtosubmit = server->requests_.front();
        logself(DEBUG, "submit request [%s] on conn objId() %u",
            reqtosubmit->url_.c_str(), conn->objId());
        conn->submit_request(reqtosubmit);
        server->requests_.pop_front();
    }
    logself(DEBUG, "done");
    return;
}

/***************************************************/

void
ConnectionManager::cnx_first_recv_byte_cb(Connection* conn)
{
    logself(DEBUG, "begin");
    if (timestamp_recv_first_byte_ != 0) {
        logself(DEBUG, "timestamp_recv_first_byte_ already set: %" PRIu64 " --> do nothing",
                timestamp_recv_first_byte_);
        return;
    }
    timestamp_recv_first_byte_ = gettimeofdayMs(nullptr);
    myassert(timestamp_recv_first_byte_ > 0);
    logself(DEBUG, "timestamp_recv_first_byte_: %" PRIu64, timestamp_recv_first_byte_);
    logself(DEBUG, "done");
}

/***************************************************/

void
ConnectionManager::cnx_request_done_cb(Connection* conn,
                                       const Request* req,
                                       const NetLoc& netloc)
{
    logself(DEBUG, "begin, req url [%s]", req->url_.c_str());

    // we don't free anything in here

    // see if there's a request waiting to be sent

    Request* reqtosubmit = nullptr;

    auto server = servers_[netloc];
    myassert(server);

    auto& requests = server->requests_;
    logself(DEBUG, "%u waiting requests", requests.size());

    if (requests.empty()) {
        logself(DEBUG, "  --> do nothing");
        goto done;
    }

    reqtosubmit = requests.front();
    logself(DEBUG, "submit request [%s] on conn objId() %u",
            reqtosubmit->url_.c_str(), conn->objId());
    conn->submit_request(reqtosubmit);
    requests.pop_front();

done:
    logself(DEBUG, "done");
    return;
}

/***************************************************/

void
ConnectionManager::cnx_error_cb(Connection* conn,
                                const NetLoc& netloc)
{
#ifdef _update_code
    logfn(SHADOW_LOG_LEVEL_WARNING, __func__, "connection error");
#endif
    handle_unusable_conn(conn, netloc);
}

/***************************************************/

void
ConnectionManager::cnx_eof_cb(Connection* conn,
                              const NetLoc& netloc)
{
#ifdef _update_code
    logfn(SHADOW_LOG_LEVEL_WARNING, __func__, "connection eof");
#endif
    handle_unusable_conn(conn, netloc);
}

/***************************************************/

void
ConnectionManager::handle_unusable_conn(Connection *conn,
                                        const NetLoc& netloc)
{
    logself(DEBUG, "begin, cnx: %d", conn->objId());

    // we should mark any requests being handled by this connection as
    // error. for now, we don't attempt to request elsewhere.

    release_conn(conn, netloc);

    /* release_conn() only removes the conn from the list. it does not
     * yet delete the conn object. so we can still get its request
     * queues.
     */
    myassert(conn->get_pending_request_queue().empty());
    retry_requests(conn->get_active_request_queue());

    logself(DEBUG, "done");
}

/***************************************************/

bool
ConnectionManager::retry_requests(queue<Request*> requests)
{
    logself(DEBUG, "begin");

    while (!requests.empty()) {
        auto req = requests.front();
        myassert(req);
        requests.pop();
        if (req->get_num_retries() == max_retries_per_resource_) {
#ifdef _update_code
            logfn(SHADOW_LOG_LEVEL_WARNING, __func__,
                  "resource [%s] has exhausted %u retries",
                  req->url_.c_str(), max_retries_per_resource_);
#endif
            notify_req_error_(req);
            continue;
        }
        req->increment_num_retries();
#ifdef _update_code
        logfn(SHADOW_LOG_LEVEL_INFO, __func__,
              "re-requesting resource [%s] for the %dth time",
              req->url_.c_str(), req->get_num_retries());
#endif
        if (req->get_body_size() > 0) {
            /* the request "body_size()" represents number of
             * contiguous bytes from 0 that we have received. so, we
             * can use that as the next first_byte_pos.
             */
            req->set_first_byte_pos(req->get_body_size());
            logself(DEBUG, "set first_byte_pos to %d",
                    req->get_first_byte_pos());
        }

        this->submit_request(req);
    }

    logself(DEBUG, "done");
    return true;
}

/***************************************************/

void
ConnectionManager::get_total_bytes(size_t& tx, size_t& rx)
{
    logself(DEBUG, "begin");

    tx = totaltxbytes_;
    rx = totalrxbytes_;

    // pair<NetLoc, Server*> kv_pair;
    for (const auto& kv_pair : servers_) {
        logself(DEBUG, "server %s:%u", 
                kv_pair.first.first.c_str(), kv_pair.first.second);
        const auto server = kv_pair.second;
        for (auto& c : server->connections_) {
            tx += c->get_total_num_sent_bytes();
            rx += c->get_total_num_recv_bytes();
        }
    }

    logself(DEBUG, "done");
}

/***************************************************/

void
ConnectionManager::reset()
{
    // we don't touch the Request* pointers.
    // pair<NetLoc, Server*> kv_pair;
    for (const auto& kv_pair: servers_) {
        logself(DEBUG, "clearing server [%s]:%u", 
                kv_pair.first.first.c_str(), kv_pair.first.second);
        const auto server = kv_pair.second;
        server->connections_.clear();
    }
    servers_.clear();

    timestamp_recv_first_byte_ = 0;
    totaltxbytes_ = totalrxbytes_ = 0;
}

/***************************************************/

static void
delete_connman(void *ptr)
{
    delete ((ConnectionManager*)ptr);
}

/***************************************************/

void
ConnectionManager::release_conn(Connection *conn,
                                const NetLoc& netloc)
{
    logself(DEBUG, "begin, releasing cnx %d", conn->objId());

    // mark the cnx for deletion later. we don't want to do it here on
    // the call stack of the cnx itself

    // conn->deleteLater(scheduleCallback);

    // remove it from active connections
    myassert(inMap(servers_, netloc));
    auto& conns = servers_[netloc]->connections_;

    auto finditer = std::find_if(
        conns.begin(), conns.end(),
        [&](shared_ptr<Connection> const& p) { return p.get() == conn; });
    myassert(finditer != conns.end());
    conns.erase(finditer);
    if (conns.size() == 0) {
        logself(DEBUG,
                "list is now empty --> remove this list from map");
        servers_.erase(netloc);
    }

    totaltxbytes_ += conn->get_total_num_sent_bytes();
    totalrxbytes_ += conn->get_total_num_recv_bytes();

    logself(DEBUG, "totaltxbytes_ %zu, totalrxbytes_ %zu",
            totaltxbytes_, totalrxbytes_);

    logself(DEBUG, "done");
}

/***************************************************/

ConnectionManager::~ConnectionManager()
{
    reset();
}
