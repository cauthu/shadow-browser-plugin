
#include <event2/event.h>
#include <memory>
#include <arpa/inet.h>

#include "../../utility/common.hpp"
#include "../../utility/myassert.h"
#include "../../utility/tcp_channel.hpp"
#include "../../utility/json_stream_channel.hpp"
#include "../../utility/ipc/io_service_ipc.hpp"
#include "ipc.hpp"


using std::unique_ptr;

int main(int argc, char **argv)
{

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(init_evbase(), event_base_free);

    /* ***************************************** */

    myio::TCPChannel::UniquePtr tcpChanForIPC(
        new myio::TCPChannel(evbase.get(), getaddr("localhost"),
                             myipc::ioservice::service_port, nullptr));
    IOServiceIPCClient::UniquePtr ipcclient(
        new IOServiceIPCClient(std::move(tcpChanForIPC)));

    /* ***************************************** */

    dispatch_evbase(evbase.get());

    return 0;
}
