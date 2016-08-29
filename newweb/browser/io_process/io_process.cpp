
#include <event2/event.h>
#include <memory>
#include <string.h>
#include <sys/time.h>

#include "../../utility/tcp_server.hpp"
#include "../../utility/myassert.h"
#include "../../utility/common.hpp"
#include "../../utility/ipc/io_service_ipc.hpp"
#include "ipc.hpp"


using std::unique_ptr;

int main(int argc, char **argv)
{
    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        init_evbase(), event_base_free);

    myio::TCPServer::UniquePtr tcpServerForIPC(
        new myio::TCPServer(evbase.get(), getaddr("localhost"),
                            myipc::ioservice::service_port, nullptr));
    IPCServer::UniquePtr ipcserver(new IPCServer(std::move(tcpServerForIPC)));

    dispatch_evbase(evbase.get());

    return 0;
}
