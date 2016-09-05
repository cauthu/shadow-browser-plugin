
#include <event2/event.h>
#include <memory>
#include <string.h>
#include <sys/time.h>

#include "../../utility/tcp_server.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "ipc.hpp"


using std::unique_ptr;

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{

#ifdef IN_SHADOW
    init_easylogging();
#endif

    LOG(INFO) << "io_process starting...";

    START_EASYLOGGINGPP(argc, argv);

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        init_evbase(), event_base_free);

    /* ***************************************** */

    const uint16_t service_port = 12345;

    myio::TCPServer::UniquePtr tcpServerForIPC(
        new myio::TCPServer(evbase.get(), getaddr("localhost"),
                            service_port, nullptr));
    IPCServer::UniquePtr ipcserver(new IPCServer(std::move(tcpServerForIPC)));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    dispatch_evbase(evbase.get());

    return 0;
}
