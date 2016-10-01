
#include "../../../utility/easylogging++.h"
#include "../../../utility/common.hpp"

#include "EventTarget.hpp"
#include "EventTypeNames.hpp"

#include <set>

using std::set;
using std::string;

namespace blink
{

static const std::set<std::string> s_all_type_names {
    EventTypeNames::load,
    EventTypeNames::DOMContentLoaded
        };

EventTarget::EventTarget(const uint32_t& instNum)
    : instNum_(instNum)
{
    // must be strictly greater 0, for 0 is "null" instnum
    CHECK_GT(instNum_, 0);
}


// void
// EventTarget::add_event_listener(const std::string& event_name,
//                                 const uint32_t& handler_scope_id)
// {
//     CHECK(inSet(s_all_type_names, event_name));


// }
                                


}
