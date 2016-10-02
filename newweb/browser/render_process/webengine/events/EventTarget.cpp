
#include "../../../utility/easylogging++.h"
#include "../../../utility/common.hpp"

#include "EventTarget.hpp"
#include "EventTypeNames.hpp"

#include <set>

#include "../webengine.hpp"

using std::set;
using std::string;

namespace blink
{

bool
EventTarget::is_supported_event_name(const std::string& name)
{
    /* shadow has difficulty dealing with some static variables, in
     * particular, our attempt to use "static std::set<std::string>
     * supported_event_names". so we're doing the check this way
     */

    if (name == EventTypeNames::load
        || name == EventTypeNames::DOMContentLoaded
        || name == EventTypeNames::readystatechange_DONE
        || name == EventTypeNames::readystatechange_complete)
    {
        return true;
    }

    LOG(WARNING) << "event name \"" << name << "\" is not yet supported";
    return false;
}

EventTarget::EventTarget(
    const uint32_t& instNum,
    Webengine* webengine
    )
    : instNum_(instNum)
    , webengine_(webengine)
{
    // must be strictly greater 0, for 0 is "null" instnum
    CHECK_GT(instNum_, 0);
    CHECK_NOTNULL(webengine_);
}

void
EventTarget::add_event_handling_scopes(
    const std::vector<std::pair<std::string, uint32_t> >& event_handling_scopes)
{
    for (auto name_to_scope_id : event_handling_scopes) {
        auto name = name_to_scope_id.first;
        auto scope_id = name_to_scope_id.second;

        VLOG(3) << "add name: [" << name << "] scope:" << scope_id;

        CHECK(is_supported_event_name(name))
            << "event name \"" << name << "\" not yet supported";

        event_handling_scopes_[name].push_back(scope_id);
    }
}

void
EventTarget::fireEventHandlingScopes(const std::string event_name)
{
    VLOG(2) << "begin, name= " << event_name;

    if (!inMap(event_handling_scopes_, event_name)) {
        VLOG(2) << "no handling scopes for this event";
    } else {
        const auto scope_ids = event_handling_scopes_[event_name];
        VLOG(2) << "num scopes: " << scope_ids.size();

        for (auto scope_id : scope_ids) {
            webengine_->execute_scope(scope_id);
        }
    }

    VLOG(2) << "done";
}

}
