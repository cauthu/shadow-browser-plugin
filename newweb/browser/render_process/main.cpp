
#include <event2/event.h>
#include <memory>
#include <arpa/inet.h>
#include <boost/lexical_cast.hpp>

#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/tcp_channel.hpp"
#include "../../utility/tcp_server.hpp"
#include "ipc_io_service.hpp"
#include "ipc_renderer.hpp"


using std::unique_ptr;

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();

    common::init_easylogging();

    uint16_t renderer_ipcport = 0;
    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--ipcListenPort")) {
            renderer_ipcport = boost::lexical_cast<uint16_t>(argv[i+1]);
        }
    }

    CHECK_GT(renderer_ipcport, 0) << "must specify a positive port to listen on to provide renderer ipc service";

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

    /// set up my ipc server
    myio::TCPServer::UniquePtr tcpServerForIPC;
    IPCServer::UniquePtr ipcserver;

    VLOG(2) << "my ipc server listens on " << renderer_ipcport;
    tcpServerForIPC.reset(
        new myio::TCPServer(
            evbase.get(), common::getaddr("localhost"),
            renderer_ipcport, nullptr));
    ipcserver.reset(
        new IPCServer(
            evbase.get(), std::move(tcpServerForIPC)));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";

    return 0;
}
