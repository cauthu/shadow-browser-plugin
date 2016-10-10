
#include "../../../utility/easylogging++.h"
#include "../../../utility/common.hpp"
#include "../../../utility/folly/ScopeGuard.h"

#include "DOMTimer.hpp"

#include "../webengine.hpp"



#define _LOG_PREFIX(inst) << "timer:" << (inst)->timer_info_.timerID << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


namespace blink
{

DOMTimer::DOMTimer(
    struct event_base* evbase,
    Webengine* webengine,
    const PageModel::DOMTimerInfo& timer_info)
    : Timer(evbase, timer_info.singleShot, NULL)
    , timer_info_(timer_info)
    , webengine_(webengine)
    , next_fired_scope_idx_(0)
{
    CHECK_NOTNULL(webengine_);
    CHECK_GT(timer_info_.fired_scope_ids.size(), 0);

    if (timer_info_.singleShot) {
        CHECK_EQ(timer_info_.fired_scope_ids.size(), 1);
    }

    // 60 seconds
    static const uint32_t max_interval_supported = 60000;

    CHECK_LT(timer_info_.interval_ms, max_interval_supported)
        << "timer interval greater than " << max_interval_supported << "?";

    vlogself(2) << "start timer, interval_ms= " << timer_info_.interval_ms;
    start(timer_info_.interval_ms);
}

void
DOMTimer::_on_event_cb()
{
    DestructorGuard dg(this);

    vlogself(2) << "begin, fired_scope_idx= " << next_fired_scope_idx_;

    CHECK_LT(next_fired_scope_idx_, timer_info_.fired_scope_ids.size())
        << "we have exhausted timer fired scopes :(";

    CHECK_NOTNULL(webengine_);
    webengine_->execute_scope(
        timer_info_.fired_scope_ids[next_fired_scope_idx_]);

    ++next_fired_scope_idx_;

    vlogself(2) << "done";

    webengine_->_do_end_of_task_work();
}

DOMTimer::~DOMTimer()
{
    vlogself(2) << "destructing";
    webengine_ = nullptr;
    cancel();
}

} // end namespace blink
