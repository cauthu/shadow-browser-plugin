
#include "timer.hpp"
#include "easylogging++.h"


#define _LOG_PREFIX(inst) << "timer= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


Timer::Timer(struct event_base *evbase,
             const bool one_shot,
             FiredCb cb,
             int priority)
    : fired_cb_(cb)
    , ev_(nullptr, event_free)
    , one_shot_(one_shot)
{
    ev_.reset(event_new(evbase, -1, (one_shot ? 0 : EV_PERSIST), s_event_cb, this));
    CHECK_NOTNULL(ev_.get());

    if (priority >= 0) {
        auto rv = event_priority_set(ev_.get(), priority);
        CHECK_EQ(rv, 0);
    }
    vlogself(2) << "timer constructed";
}

bool
Timer::start(const struct timeval *tv, const bool assert_not_running)
{
    CHECK_NOTNULL(tv);

    if (!one_shot_) {
        CHECK(timerisset(tv)) << "a repeating timer with 0 delay??";
    }

    if (is_running()) {
        if (assert_not_running) {
            logself(FATAL)
                << "timer is pending/firing; maybe you want restart()?";
        } else {
            return false;
        }
    }

    auto rv = event_add(ev_.get(), tv);
    CHECK_EQ(rv, 0);

    vlogself(2) << "timer started";
    return true;
}

bool
Timer::start(const uint32_t msec, const bool assert_not_running)
{
    struct timeval tv;
    // get the whole seconds
    tv.tv_sec = (msec) / 1000;
    // mod-1000 to get the sub-second in millisecond, then multiply
    // 1000 to get microseconds
    tv.tv_usec = (msec % 1000) * 1000;
    return start(&tv, assert_not_running);
}

void
Timer::cancel()
{
    auto rv = event_del(ev_.get());
    CHECK_EQ(rv, 0);
}

void
Timer::restart(const struct timeval *tv)
{
    cancel();
    start(tv);
}

bool
Timer::is_running() const
{
    // since we specify EV_TIMEOUT, event_pending will return
    // EV_TIMEOUT if it's pending for EV_TIMEOUT
    auto rv = event_pending(ev_.get(), EV_TIMEOUT, nullptr);
    return (rv != 0);
}

Timer::~Timer()
{
    vlogself(2) << "timer destroyed";
    ev_.reset();
}


void
Timer::s_event_cb(int, short, void* arg)
{
    Timer* timer = (Timer*)arg;
    timer->_on_event_cb();
}

void
Timer::_on_event_cb()
{
    // we don't need destructorguard because we don't really do
    // anything else. so if the user destroys us, during the callback,
    // will be fine (specifically, it's ok for our destructor to call
    // event_del while the event is active
    //
    // update: due to issue
    // https://bitbucket.org/hatswitch/shadow-plugin-extras/issues/9/another-crashing-issue,
    // i'll just use destructor guard here even though i don't know
    // for sure if this is the cause
    
    DestructorGuard dg(this);
    fired_cb_(this);
}
