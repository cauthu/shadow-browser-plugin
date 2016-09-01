
#include <event2/event.h>
#include <memory>
#include <arpa/inet.h>

#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/tcp_channel.hpp"
#include "../../utility/json_stream_channel.hpp"
#include "../../utility/ipc/io_service_ipc.hpp"
#include "ipc.hpp"


using std::unique_ptr;

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{

#ifdef IN_SHADOW
    init_easylogging();
#endif

    LOG(INFO) << "render_process starting...";

    START_EASYLOGGINGPP(argc, argv);

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        init_evbase(), event_base_free);

    /* ***************************************** */

    myio::TCPChannel::UniquePtr tcpChanForIPC(
        new myio::TCPChannel(evbase.get(), getaddr("localhost"),
                             myipc::ioservice::service_port, nullptr));
    IOServiceIPCClient::UniquePtr ipcclient(
        new IOServiceIPCClient(std::move(tcpChanForIPC)));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    dispatch_evbase(evbase.get());

    return 0;
}
