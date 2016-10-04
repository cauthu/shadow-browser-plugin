
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

#include "../utility/tcp_server.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"

#include "ipc.hpp"
#include "csp/csp.hpp"
#include "ssp/ssp.hpp"


/*
 *
 * when run outside shadow, we don't start the ipc server, and we
 * immediately try to establish the tunnel to the ssp.
 *
 * this is more convenient because we just start the process and it
 * should autoamtically try to get ready, and when we're finished with
 * a page load we can kill the process and restart it.
 *
 * inside shadow we cannot exit processes and launch them again ---
 * shadow only supports launching once at the beginning --- so we need
 * the ipc in order to reset the tunnel to the ssp
 * 
 */


static void
s_on_csp_ready(csp::ClientSideProxy*)
{
    LOG(INFO) << "CSP is ready";
}


INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();

    common::init_easylogging();

    std::string ssp_host;
    uint16_t ssp_port = 0;
    /* for client or sever, depends on is_client bool below */
    uint16_t listenport = 0;
    uint16_t torPort = 0;

    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--ssp")) {
            // should be host:port
            /* NOTE that due to issue
             * https://bitbucket.org/hatswitch/shadow-plugin-extras/issues/3/
             * the csp will do local lookup of the ssp's ip address
             * and give that to Tor proxy
             */
            std::string ssp_host_port = argv[i+1];
            const auto colon_pos = ssp_host_port.find(':');
            CHECK_GT(colon_pos, 0);
            ssp_host = ssp_host_port.substr(0, colon_pos);
            ssp_port = boost::lexical_cast<uint16_t>(ssp_host_port.substr(colon_pos+1));
        } else if (!strcmp(argv[i], "--port")) {
            listenport = boost::lexical_cast<uint16_t>(argv[i+1]);
        } else if (!strcmp(argv[i], "--torPort")) {
            /* will connect to local (i.e., "localhost") tor proxy at
             * this port */
            torPort = boost::lexical_cast<uint16_t>(argv[i+1]);
        }
    }

    const bool is_client = !ssp_host.empty();

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "TransportProxy starting...";

    std::unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    if (listenport == 0) {
        listenport = is_client
                     ? common::ports::client_side_transport_proxy
                     : common::ports::server_side_transport_proxy;
    }

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

        VLOG(2) << "ssp: [" << ssp_host << "]:" << ssp_port;
        VLOG(2) << "torPort: " << torPort;
        csp.reset(new csp::ClientSideProxy(
                      evbase.get(),
                      std::move(tcpserver),
                      ssp_host.c_str(),
                      ssp_port,
                      torPort ? common::getaddr("localhost") : 0,
                      torPort));

#ifdef IN_SHADOW
        const uint16_t ipcport = common::ports::transport_proxy_ipc;
        VLOG(2) << "ipc server listens on " << ipcport;
        tcpServerForIPC.reset(
            new myio::TCPServer(
                evbase.get(), common::getaddr("localhost"),
                ipcport, nullptr));
        ipcserver.reset(
            new IPCServer(
                evbase.get(), std::move(tcpServerForIPC), std::move(csp)));
#else
        const auto rv = csp->establish_tunnel(
            boost::bind(s_on_csp_ready, _1),
            true);
#endif

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
