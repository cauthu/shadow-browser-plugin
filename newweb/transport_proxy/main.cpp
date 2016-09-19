
#include "../utility/tcp_server.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"

#include "ipc.hpp"
#include "csp/csp.hpp"
#include "ssp/ssp.hpp"


INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();

    common::init_easylogging();

    std::string ssp_host;
    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--ssp")) {
            ssp_host = argv[i+1];
        }
    }

    const bool is_client = !ssp_host.empty();

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "TransportProxy starting...";

    std::unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    const uint16_t listenport = is_client
                                ? common::ports::client_side_transport_proxy
                                : common::ports::server_side_transport_proxy;

    VLOG(2) << "listen port " << listenport;

    /* ***************************************** */

    csp::ClientSideProxy::UniquePtr csp;
    ssp::ServerSideProxy::UniquePtr ssp;

    myio::TCPServer::UniquePtr tcpServerForIPC;
    IPCServer::UniquePtr ipcserver;

    if (is_client) {
        /* tcpserver to accept connections from clients */
        myio::TCPServer::UniquePtr tcpserver(
            new myio::TCPServer(evbase.get(),
                                common::getaddr("localhost"),
                                listenport, nullptr));

        VLOG(2) << "ssp host: [" << ssp_host << "]";
        csp.reset(new csp::ClientSideProxy(
                      evbase.get(),
                      std::move(tcpserver),
                      common::getaddr(ssp_host.c_str()),
                      common::ports::server_side_transport_proxy,
                      0, 0));

        const uint16_t ipcport = common::ports::transport_proxy_ipc;
        VLOG(2) << "ipc server listens on " << ipcport;
        tcpServerForIPC.reset(
            new myio::TCPServer(
                evbase.get(), common::getaddr("localhost"),
                ipcport, nullptr));
        ipcserver.reset(
            new IPCServer(
                evbase.get(), std::move(tcpServerForIPC), std::move(csp)));
    } else {
        /* tcpserver to accept connections from CSPs */
        myio::TCPServer::UniquePtr tcpserver(
            new myio::TCPServer(evbase.get(),
                                INADDR_ANY,
                                listenport, nullptr));

        ssp.reset(new ssp::ServerSideProxy(evbase.get(),
                                      std::move(tcpserver)));
    }

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";
    return 0;
}
