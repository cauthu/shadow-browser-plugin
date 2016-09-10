
#include "timer.hpp"
#include "easylogging++.h"

Timer::Timer(struct event_base *evbase,
             const bool one_shot,
             FiredCb cb)
    : fired_cb_(cb)
    , one_shot_(one_shot)
{
    auto rv = event_assign(
        &ev_, evbase, -1, (one_shot ? 0 : EV_PERSIST), s_event_cb, this);
    CHECK_EQ(rv, 0);
}

void
Timer::start(const struct timeval *tv)
{
    if (!one_shot_) {
        CHECK(timerisset(tv)) << "a repeating timer with 0 delay??";
    }

    // true means either pending or active: need to specify EV_TIMEOUT
    // to query whether timeout status
    auto rv = event_pending(&ev_, EV_TIMEOUT, nullptr);
    CHECK_EQ(rv, 0) << "timer is pending/firing; maybe you want restart()?";

    rv = event_add(&ev_, tv);
    CHECK_EQ(rv, 0);
}

void
Timer::cancel()
{
    auto rv = event_del(&ev_);
    CHECK_EQ(rv, 0);
}

void
Timer::restart(const struct timeval *tv)
{
    cancel();
    start(tv);
}

Timer::~Timer()
{
    event_del(&ev_);
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
    fired_cb_(this);
}
