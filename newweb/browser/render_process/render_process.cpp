
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
    init_easylogging();

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "render_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        init_evbase(), event_base_free);

    /* ***************************************** */

    const uint16_t ioservice_port = 12345;

    myio::TCPChannel::UniquePtr tcpChanForIPC(
        new myio::TCPChannel(evbase.get(), getaddr("localhost"),
                             ioservice_port, nullptr));
    IOServiceIPCClient::UniquePtr ipcclient(
        new IOServiceIPCClient(std::move(tcpChanForIPC)));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";

    return 0;
}
