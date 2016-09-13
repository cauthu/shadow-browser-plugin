
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
    common::init_common();

    common::init_easylogging();

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "io_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    const uint16_t service_port = 12345;

    NetConfig netconf(common::getaddr("127.0.0.1"),
                      common::ports::client_side_transport_proxy,
                      false, false);

    myio::TCPServer::UniquePtr tcpServerForIPC(
        new myio::TCPServer(evbase.get(), common::getaddr("localhost"),
                            service_port, nullptr));
    IPCServer::UniquePtr ipcserver(
        new IPCServer(evbase.get(), std::move(tcpServerForIPC), &netconf));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";
    return 0;
}
