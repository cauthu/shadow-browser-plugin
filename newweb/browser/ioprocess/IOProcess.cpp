
#include <event2/event.h>
#include <memory>
#include <string.h>
#include <sys/time.h>

#include "../../utility/tcp_server.hpp"
#include "../../utility/myassert.h"
#include "ipc.hpp"


using std::unique_ptr;

int main(int argc, char **argv)
{
    unique_ptr<struct event_config, void(*)(struct event_config*)> evconfig(
        event_config_new(), event_config_free);
    myassert(evconfig);

    // we mean to run single-threaded, so we don't need locks
    myassert(0 == event_config_set_flag(evconfig.get(), EVENT_BASE_FLAG_NOLOCK));

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        event_base_new_with_config(evconfig.get()), event_base_free);
    myassert(evbase);

    /* double check a few things */
    auto chosen_method = event_base_get_method(evbase.get());
    if (strcmp("epoll", chosen_method)) {
        printf("ERROR: libevent is using \"%s\"; we want \"epoll.\"\n", chosen_method);
        exit(1);
    }

    auto features = event_base_get_features(evbase.get());
    if (! (features & EVENT_BASE_FLAG_NOLOCK)) {
        printf("ERROR: libevent is using locks; we do not want that.\n");
        exit(1);
    }

    IPCServer::UniquePtr ipcserver(new IPCServer());
    myio::TCPServer::UniquePtr tcpServerForIPC(
        new myio::TCPServer(evbase.get(), 0, 1999, ipcserver.get()));
    
    /******  run the loop until told to stop ******/
    /* EVLOOP_NO_EXIT_ON_EMPTY not yet available, so we have to
     * install a dummy persistent timer to always have some event
     */
    struct timeval tv = {0};
    tv.tv_sec = 60;
    tv.tv_usec = 0;
    struct event *dummy_work_ev = event_new(
        evbase.get(), -1, EV_PERSIST | EV_TIMEOUT, [](int, short, void*){}, nullptr);
    myassert(dummy_work_ev);
    auto rv = event_add(dummy_work_ev, &tv);
    myassert(!rv);

    event_base_dispatch(evbase.get());

    if (dummy_work_ev) {
        event_free(dummy_work_ev);
    }
    return 0;
}
