
#include "../../../../utility/easylogging++.h"

#include "Resource.hpp"


#define _LOG_PREFIX(inst) << "resource= " << (inst)->objId() << ": instNum= " << instNum_ << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


namespace blink
{


Resource::Resource(const uint32_t& instNum)
    : instNum_(instNum)
    , counted_for_doc_load_event(false)
    , load_state_(LoadState::INITIAL)
    , numBytesRecv_(0)
    , errored_(false)
{
}


void
Resource::load()
{
    CHECK_EQ(load_state_, LoadState::INITIAL);
    load_state_ = LoadState::LOADING;
}

void
Resource::appendData(size_t length)
{
    numBytesRecv_ += length;
    _notify_new_data(length);
}

void
Resource::addClient(ResourceClient* client)
{
    CHECK(!isFinished());

    m_clients.insert(client);
}

void
Resource::finish(bool success)
{
    CHECK_EQ(load_state_, LoadState::LOADING);
    load_state_ = LoadState::FINISHED;
    errored_ = !success;
    _notify_finished(success);
}

// void
// Resource::error()
// {
//     CHECK_EQ(load_state_, LoadState::LOADING);
//     load_state_ = LoadState::FINISHED;
//     errored_ = true;
//     _notify_finished(false);
// }

void
Resource::_notify_finished(bool success)
{
    /* notify clients that we're finished */

    DestructorGuard dg(this);

    for (auto client : m_clients) {
        client->notifyFinished(this, success);
    }

}

void
Resource::_notify_new_data(const size_t& length)
{
    DestructorGuard dg(this);

    for (auto client : m_clients) {
        client->dataReceived(this, length);
    }

}

}
