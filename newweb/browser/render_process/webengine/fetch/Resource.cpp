
#include "../../../../utility/easylogging++.h"

#include "Resource.hpp"
#include "../webengine.hpp"


#define _LOG_PREFIX(inst) << "res:" << instNum() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


namespace blink
{


Resource::Resource(const PageModel::ResourceInfo& res_info,
                   Webengine* webengine,
                   ResourceFetcher* fetcher)
    : res_info_(res_info)
    , webengine_(webengine)
    , resource_fetcher_(fetcher)
    , load_state_(LoadState::INITIAL)
    , cumulative_resp_body_bytes_(0)
    , errored_(false)
    , current_req_chain_idx_(-1)
    , current_req_body_bytes_recv_(0)
    , first_byte_time_ms_(0)
{
    CHECK_NOTNULL(webengine_);
}


void
Resource::load()
{
    vlogself(2) << "really starts loading resource";
    CHECK_EQ(load_state_, LoadState::INITIAL);
    load_state_ = LoadState::LOADING;

    if (part_of_page_loaded_check()) {
        resource_fetcher_->incrementRequestCount(this);
    }

    _load_next_chain_entry();
}

void
Resource::_load_next_chain_entry()
{
    ++current_req_chain_idx_;
    CHECK_GE(current_req_chain_idx_, 0);
    CHECK_LT(current_req_chain_idx_,
             res_info_.req_chain.size());
    CHECK_EQ(load_state_, LoadState::LOADING);

    vlogself(2) << "starting loading request chain entry: "
                << current_req_chain_idx_;

    webengine_->ioservice_request_resource(
        res_info_.req_chain[current_req_chain_idx_], this);

    webengine_->renderer_notify_RequestWillBeSent(
        instNum(), current_req_chain_idx_);
}

bool
Resource::_receiving_real_resource() const
{
    return current_req_chain_idx_ == (res_info_.req_chain.size() - 1);
}

void
Resource::receivedResponseMeta(const uint64_t first_byte_time_ms)
{
    if (!current_req_chain_idx_) {
        CHECK_EQ(first_byte_time_ms_, 0);
        first_byte_time_ms_ = first_byte_time_ms;
    }
}

void
Resource::appendData(size_t length)
{
    cumulative_resp_body_bytes_ += length;
    current_req_body_bytes_recv_ += length;
    vlogself(2) << length << " more body bytes, total so far: "
                << current_req_body_bytes_recv_;
    // check that we're not receiving more data than we asked for
    CHECK_LE(current_req_body_bytes_recv_,
             res_info_.req_chain[current_req_chain_idx_].resp_body_size);
    if (_receiving_real_resource()) {
        _notify_new_data(length);
    }
}

void
Resource::addClient(ResourceClient* client)
{
    CHECK(!isFinished());

    m_clients.insert(client);
}

void
Resource::removeClient(ResourceClient* client)
{
    m_clients.erase(client);
}

void
Resource::finish(bool success)
{
    vlogself(2) << "begin, success= " << success;

    CHECK_EQ(load_state_, LoadState::LOADING);

    webengine_->renderer_notify_RequestFinished(
        instNum(), current_req_chain_idx_, success);

    if (success) {
        CHECK_EQ(current_req_body_bytes_recv_,
                 res_info_.req_chain[current_req_chain_idx_].resp_body_size)
            << "expected= "
            << res_info_.req_chain[current_req_chain_idx_].resp_body_size
            << " actual= " << current_req_body_bytes_recv_;
        current_req_body_bytes_recv_ = 0;
        if (_receiving_real_resource()) {
            // we are really finished
            load_state_ = LoadState::FINISHED;
            errored_ = !success;
            _really_did_succeed();
        } else {
            _load_next_chain_entry();
        }
    } else {
        logself(WARNING) << "request failed";
        errored_ = !success;
        _notify_finished(success);
    }

    vlogself(2) << "done";
}

void
Resource::_really_did_succeed()
{
    DestructorGuard dg(this);
    _notify_finished(true);
}

void
Resource::_notify_finished(bool success)
{
    /* notify clients that we're finished */

    vlogself(2) << "begin, success= " << success
                << " num clients= " << m_clients.size();
    DestructorGuard dg(this);

    // the order can be important; double-check if you change the order
    //
    // first: decrement the request count
    if (part_of_page_loaded_check()) {
        resource_fetcher_->decrementRequestCount(this);
    }

    // then, notify finished
    for (auto client : m_clients) {
        client->notifyFinished(this, success);
    }

    vlogself(2) << "done";
}

void
Resource::_notify_new_data(const size_t& length)
{
    DestructorGuard dg(this);

    vlogself(3) << "begin, num clients= " << m_clients.size();

    for (auto client : m_clients) {
        client->dataReceived(this, length);
    }

    vlogself(3) << "done";
}

Resource::~Resource()
{
    vlogself(2) << "destructing";
}

inline const uint32_t&
Resource::instNum() const
{
    return res_info_.instNum;
}

} // end namespace blink
