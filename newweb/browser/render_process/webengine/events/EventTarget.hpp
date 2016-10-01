#ifndef EventTarget_hpp
#define EventTarget_hpp

#include <set>
#include <string>
#include <vector>

#include "../../../../utility/object.hpp"
#include "../../../../utility/easylogging++.h"



namespace blink {

class EventTarget : public Object
{
public:
    typedef std::unique_ptr<EventTarget, Destructor> UniquePtr;


    const uint32_t& instNum() const { return instNum_; }

    // /* event name like "load" or "DOMContentLoaded" */
    // virtual void add_event_listener(const std::string& event_name,
    //                                 const uint32_t& handler_scope_id);

protected:

    explicit EventTarget(const uint32_t& instNum);

    virtual ~EventTarget() = default;

    ////////

    const uint32_t instNum_; // from the page model

    /* sets of scope ids */

    /* map from event type names like "load", "DOMContentLoaded",
     * etc. to execution scope id
     */
    std::set<std::string, std::vector<uint32_t> > event_handling_scopes_;

};

} // namespace blink

#endif // EventTarget_hpp
