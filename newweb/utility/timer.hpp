#ifndef timer_hpp
#define timer_hpp

#include <memory>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <boost/function.hpp>
#include <sys/time.h>

#include "object.hpp"


class Timer : public Object
{
public:
    typedef std::unique_ptr<Timer, Destructor> UniquePtr;
    typedef boost::function<void(Timer*)> FiredCb;

    /* if one_shot is false, then it's a repeating timer.
     *
     * constructing the timer here does not activate it. use start()
     * to actually start the timer
     *
     * "priority" will be set on the event if >= 0. libevent uses 0 to
     * be the highest priority
     */
    Timer(struct event_base *evbase, bool one_shot, FiredCb cb,
          int priority=-1);

    /*
     *
     * NOTE: shadow has a bug such that a repeating timer (i.e.,
     * one_shot = false) with tv=0 (i.e., tv_sec = tv_usec = 0) will
     * fire only ONCE----running outside shadow, it correctly fires
     * repeatedly. this should not be a problem for us because we
     * really shouldn't be using a repeating timer with 0 delay anyway.
     *
     */

    /* should call start() only if it's not already started/firing.
     *
     * cancel() and restart() can be called anytime.
     */
    void start(const struct timeval *tv);
    void start(const uint32_t msec);
    void cancel();
    void restart(const struct timeval *tv);

    /* 
     * this will return true if the timer is scheduled to fire in the
     * future or is firing right now
     */
    bool is_running() const;

protected:

    virtual ~Timer();

    virtual void _on_event_cb();
    static void s_event_cb(int, short, void*);

    //////

    FiredCb fired_cb_;

    /* not using a pointer, maybe a lil faster */
    std::unique_ptr<struct event, void(*)(struct event*)> ev_;
    const bool one_shot_; // to catch potential bugs
};


#endif /* timer_hpp */
