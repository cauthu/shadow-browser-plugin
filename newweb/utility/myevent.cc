#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <strings.h>
#include "myassert.h"
#include <unistd.h>

#include "common.hpp"
#include "myevent.hpp"

#ifdef ENABLE_MY_LOG_MACROS
#define mylogDEBUG(fmt, ...)                                            \
    do {                                                                \
            log_(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "line %u: " fmt, \
                 __LINE__, ##__VA_ARGS__);                              \
    } while (0)
#else
/* no op */
#define mylogDEBUG(fmt, ...)

#endif


myevent_socket_t::myevent_socket_t(
    struct event_base* evbase, const int fd, mev_data_cb readcb,
    mev_data_cb writecb, mev_event_cb eventcb,
    void *user_data)
    : evbase_(evbase), fd_(fd), close_fd_(true)
    , readcb_(readcb), writecb_(writecb), eventcb_(eventcb)
    , user_data_(user_data), state_(MEV_STATE_INIT)
    , ev_({nullptr, event_free})
{
    myassert(fd_ >= 0);
    mylogDEBUG("new socket event: fd is %d", fd);
}

static void
g_event_cb(evutil_socket_t fd, short what, void *arg)
{
    myevent_socket_t* mev = (myevent_socket_t*)arg;
    myassert(mev->get_fd() == fd);
    mev->trigger(what);
}

int
myevent_socket_t::start_monitoring()
{
    short what = EV_PERSIST;

    if (readcb_) {
        what |= EV_READ;
    }

    // (epoll notifies of "connected" as a pollout, so if connecting,
    // need pollout even if user dont want writecb.)
    //
    myassert(0); // DOUBLECHECK: does libevent require EV_WRITE to
                 // notify when socket becomes connected?
    if (state_ == MEV_STATE_CONNECTING || writecb_) {
        what |= EV_WRITE;
    }

    mylogDEBUG("add event. what = %u", what);

    myassert(!ev_);
    ev_.reset(event_new(evbase_, fd_, what, g_event_cb, this));
    myassert(ev_);

    const int rv = event_add(ev_.get(), NULL);
    if (rv) {
        state_ = MEV_STATE_ERROR;
        return -1;
    }
    return 0;
}

int
myevent_socket_t::socket_connect(struct sockaddr *address, int addrlen)
{
    mylogDEBUG("begin");

    myassert (state_ == MEV_STATE_INIT);

    // short what = 0;

    mylogDEBUG("readcb: %X, writecb: %X, eventcb: %X",
               readcb_, writecb_, eventcb_);
    
    mylogDEBUG("calling connect()... ");
    int rv = connect(fd_, address, addrlen);
    if (rv == 0) {
        state_ = MEV_STATE_CONNECTED;
        /* it's immediately connected -> just watch for what the user
         * wants */
        mylogDEBUG("it immediately connected");
    } else if (rv == -1) {
        if (errno == EINPROGRESS) {
            state_ = MEV_STATE_CONNECTING;
            rv = 0;
        } else {
            state_ = MEV_STATE_ERROR;
            mylogDEBUG("connect error");
            goto done;
        }
    }

    rv = start_monitoring();
    if(rv) {
        state_ = MEV_STATE_ERROR;
    }

done:
    mylogDEBUG("done, returning rv %d", rv);
    return rv;
}

myevent_socket_t::~myevent_socket_t()
{
    mylogDEBUG("begin destructor");
    if (close_fd_) {
        mylogDEBUG("closing fd %d", fd_);
        if (fd_ != -1) {
            close(fd_);
        }
    } else {
        mylogDEBUG("NOT closing fd %d", fd_);
    }
    fd_ = -1;
}

int
myevent_socket_t::trigger(const short what)
{
    myassert((state_ == MEV_STATE_CONNECTING) || (state_ == MEV_STATE_CONNECTED));

    mylogDEBUG("trigger event fd %d, what %X", fd_, what);

    if (what & EV_WRITE) {
        mylogDEBUG("EV_WRITE event");
        if (state_ == MEV_STATE_CONNECTING) {
            /* can write while in state connecting -> call the
             * "eventcb"
             */
            state_ = MEV_STATE_CONNECTED;
            if (eventcb_) {
                eventcb_(fd_, MEV_EVENT_CONNECTED, user_data_);
            }
        } else {
            if (writecb_) {
                writecb_(fd_, user_data_);
            }
        }
    }

    if (what & EV_READ) {
        mylogDEBUG("EV_READ event");
        if (readcb_) {
            readcb_(fd_, user_data_);
        }
    }

    mylogDEBUG("done");

    return 0;
}

void
myevent_socket_t::set_readcb(mev_data_cb readcb)
{
    // set the new read cb, but the other things stay the same
    setcb(readcb, writecb_, eventcb_, user_data_);
}

void
myevent_socket_t::set_writecb(mev_data_cb writecb)
{
    // set the new write cb, but the other things stay the same
    setcb(readcb_, writecb, eventcb_, user_data_);
}

void
myevent_socket_t::setcb(mev_data_cb readcb, mev_data_cb writecb,
                        mev_event_cb eventcb, void *user_data)
{
    readcb_ = readcb;
    writecb_ = writecb;
    eventcb_ = eventcb;
    user_data_ = user_data;

    short new_what = EV_PERSIST;

    if (readcb_) {
        new_what |= EV_READ;
    }
    if (writecb_) {
        new_what |= EV_WRITE;
    }

    /* todo: maybe can use event_pending() to see if the event is
     * already currently registered with the same 'what' then we don't
     * need to do anything
     */

    int rv = event_del(ev_.get());
    myassert(!rv);

    rv = event_add(ev_.get(), NULL);
    myassert(!rv);
}
