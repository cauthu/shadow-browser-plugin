
#include "../utility/tcp_server.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"

#include "csp/csp.hpp"


INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();

    common::init_easylogging();

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "TransportProxy starting...";

    std::unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    const uint16_t listenport = common::ports::client_side_transport_proxy;

    VLOG(2) << "listen port " << listenport;

    /* ***************************************** */

    myio::TCPServer::UniquePtr tcpserver(
        new myio::TCPServer(evbase.get(), INADDR_ANY, listenport, nullptr));
    CSPHandler::UniquePtr csp(
        new CSPHandler(evbase.get(),
                       std::move(tcpserver),
                       0, 0, 0, 0));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";
    return 0;
}
