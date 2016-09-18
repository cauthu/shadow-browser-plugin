
#include <event2/event.h>
#include <memory>
#include <arpa/inet.h>

#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/tcp_channel.hpp"
#include "ipc.hpp"


using std::unique_ptr;

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();

    common::init_easylogging();

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "render_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    const uint16_t ioservice_port = common::ports::io_service_ipc;

    myio::TCPChannel::UniquePtr tcpch1(
        new myio::TCPChannel(evbase.get(), common::getaddr("localhost"),
                             ioservice_port, nullptr));
    IOServiceIPCClient::UniquePtr io_ipc_client(
        new IOServiceIPCClient(evbase.get(), std::move(tcpch1)));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";

    return 0;
}
