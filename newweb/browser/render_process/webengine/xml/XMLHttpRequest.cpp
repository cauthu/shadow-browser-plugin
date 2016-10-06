
#include "XMLHttpRequest.hpp"

#include "../webengine.hpp"
#include "../fetch/ResourceFetcher.hpp"
#include "../events/EventTypeNames.hpp"


using std::shared_ptr;


#define _LOG_PREFIX(inst) << "xhr:" << (inst)->info_.instNum << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


namespace blink
{

XMLHttpRequest::XMLHttpRequest(
    Webengine* webengine,
    ResourceFetcher* resource_fetcher,
    const PageModel::XMLHttpRequestInfo& info
    )
    : EventTarget(info.instNum, webengine)
    , webengine_(webengine)
    , resource_fetcher_(resource_fetcher)
    , info_(info)
    , load_state_(XhrState::INITIAL)
    , current_res_chain_idx_(-1)
{
    add_event_handling_scopes(info.event_handling_scopes);
}

void
XMLHttpRequest::send()
{
    vlogself(2) << "begin sending";
    CHECK_EQ(load_state_, XhrState::INITIAL);
    load_state_ = XhrState::LOADING;
    _load_next_chain_entry();
    vlogself(2) << "done";
}

void
XMLHttpRequest::_load_next_chain_entry()
{
    ++current_res_chain_idx_;
    CHECK_GE(current_res_chain_idx_, 0);
    CHECK_LT(current_res_chain_idx_,
             info_.res_chain.size());
    CHECK_EQ(load_state_, XhrState::LOADING);

    vlogself(2) << "start loading resource chain entry: "
                << current_res_chain_idx_;

    shared_ptr<Resource> resource = resource_fetcher_->getResource(
        info_.res_chain[current_res_chain_idx_]);
    CHECK_NOTNULL(resource.get());

    // for now we assume xhr's load their own resources, and not share
    // resources with other parts of system, and thus each resource in
    // the chain must not be loading or finished
    CHECK(!resource->isFinished());
    CHECK(!resource->isLoading());

    resource->load();
    resource->addClient(this);
}

bool
XMLHttpRequest::_receiving_final_resource() const
{
    return current_res_chain_idx_ == (info_.res_chain.size() - 1);
}

void
XMLHttpRequest::_really_did_succeed()
{
    DestructorGuard dg(this);
    EventTarget::fireEventHandlingScopes(
        EventTypeNames::readystatechange_DONE);
    EventTarget::fireEventHandlingScopes(
        EventTypeNames::load);
}

void
XMLHttpRequest::notifyFinished(Resource* resource, bool success)
{
    vlogself(2) << "begin, success= " << success;

    CHECK_EQ(load_state_, XhrState::LOADING);

    if (success) {
        if (_receiving_final_resource()) {
            // we are really finished
            load_state_ = XhrState::FINISHED;
            _really_did_succeed();
        } else {
            _load_next_chain_entry();
        }
    } else {
        logself(WARNING) << "XHR failed";
        // errored_ = !success;
        // _notify_finished(success);
    }

    vlogself(2) << "done";
}

} // end namespace
