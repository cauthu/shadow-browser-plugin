
#include <event2/event.h>
#include <memory>
#include <string.h>
#include <sys/time.h>
#include <boost/lexical_cast.hpp>

#include "../../utility/tcp_server.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "ipc.hpp"


using std::unique_ptr;

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();

    common::init_easylogging();

    std::string socks5_host;
    uint16_t socks5_port = 0;

    uint16_t ioserviceIpcPort = common::ports::io_service_ipc;

    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--socks5-hostname")) {
            // should be host:port
            const std::string socks5_host_port = argv[i+1];
            const auto colon_pos = socks5_host_port.find(':');
            if (colon_pos > 0) {
                socks5_host = socks5_host_port.substr(0, colon_pos);
                socks5_port = boost::lexical_cast<uint16_t>(
                    socks5_host_port.substr(colon_pos+1));
            } else {
                socks5_host = socks5_host_port;
                socks5_port = 1080;
            }
        }
        else if (!strcmp(argv[i], "--ioserviceIpcPort")) {
            ioserviceIpcPort = boost::lexical_cast<uint16_t>(argv[i+1]);
        }
    }

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "io_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    NetConfig netconf;
    if (!socks5_host.empty()) {
        LOG(INFO) << "using socks5 proxy: [" << socks5_host << "]:" << socks5_port;
        CHECK_GT(socks5_port, 0);
        netconf.set_socks5_addr(common::getaddr(socks5_host.c_str()));
        netconf.set_socks5_port(socks5_port);
    }

    myio::TCPServer::UniquePtr tcpServerForIPC(
        new myio::TCPServer(evbase.get(), common::getaddr("localhost"),
                            ioserviceIpcPort, nullptr));
    IPCServer::UniquePtr ipcserver(
        new IPCServer(evbase.get(), std::move(tcpServerForIPC), &netconf));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";
    return 0;
}
