
#include <event2/event.h>
#include <memory>
#include <arpa/inet.h>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <unistd.h>
#include <time.h>

using std::cout;
using std::endl;


#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/tcp_channel.hpp"
#include "../../utility/tcp_server.hpp"
#include "ipc_io_service.hpp"
#include "ipc_renderer.hpp"

#include "webengine/webengine.hpp"



using std::unique_ptr;

static IOServiceIPCClient::UniquePtr io_service_ipc_client;
static IPCServer::UniquePtr ipcserver;
static blink::Webengine::UniquePtr webengine;

INITIALIZE_EASYLOGGINGPP

static void
s_on_io_service_ipc_client_status(IOServiceIPCClient::ChannelStatus status,
                                  struct event_base* evbase,
                                  uint16_t renderer_ipcport)
{
    CHECK_EQ(status, IOServiceIPCClient::ChannelStatus::READY);

    VLOG(2) << "ioservice ipc client is ready";

    webengine.reset(
        new blink::Webengine(io_service_ipc_client.get()));

    /// set up my ipc server
    myio::TCPServer::UniquePtr tcpServerForIPC;

    VLOG(2) << "my ipc server listens on " << renderer_ipcport;
    tcpServerForIPC.reset(
        new myio::TCPServer(
            evbase, common::getaddr("localhost"),
            renderer_ipcport, nullptr));
    ipcserver.reset(
        new IPCServer(
            evbase, std::move(tcpServerForIPC), webengine.get()));

    VLOG(2) << "ioservice ip client: " << io_service_ipc_client.get()
            << " , my ipcserver: " <<  ipcserver.get();
}

/*==================================================*/


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

    io_service_ipc_client.reset(
        new IOServiceIPCClient(
            evbase.get(), std::move(tcpch1),
            boost::bind(s_on_io_service_ipc_client_status,
                        _2, evbase.get(), renderer_ipcport)));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";

    return 0;
}
