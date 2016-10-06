#ifndef EventTarget_hpp
#define EventTarget_hpp

#include <set>
#include <map>
#include <string>
#include <vector>

#include "../../../../utility/object.hpp"
#include "../../../../utility/easylogging++.h"



namespace blink {

    class Webengine;


class EventTarget : public Object
{
public:
    typedef std::unique_ptr<EventTarget, Destructor> UniquePtr;

    const uint32_t& instNum() const { return instNum_; }

    /* vector of event name like "load" or "DOMContentLoaded" and
     * scope id pairs
     */
    virtual void add_event_handling_scopes(
        const std::vector<std::pair<std::string, uint32_t> >&);

    static bool is_supported_event_name(const std::string&);

protected:

    explicit EventTarget(const uint32_t& instNum,
                         Webengine*);

    virtual ~EventTarget();

    void fireEventHandlingScopes(const std::string event_name);

    ////////

    const uint32_t instNum_; // from the page model
    Webengine* webengine_;

    /* sets of scope ids */

    /* map from event type names like "load", "DOMContentLoaded",
     * etc. to execution scope id
     */
    std::map<std::string, std::vector<uint32_t> > event_handling_scopes_;

};

} // namespace blink

#endif // EventTarget_hpp
