
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
using std::vector;
using std::string;
using std::pair;

struct MyConfig
{
    MyConfig()
        : socks5_port(0)
        , ioservice_ipcport(common::ports::io_service_ipc)
    {
    }

    std::string socks5_host;
    uint16_t socks5_port;
    uint16_t ioservice_ipcport;
};

static void
set_my_config(MyConfig& conf,
              const vector<pair<string, string> >& name_value_pairs)
{
    for (auto nv_pair : name_value_pairs) {
        const auto& name = nv_pair.first;
        const auto& value = nv_pair.second;

        if (name == "socks5-hostname") {
            common::parse_host_port(value, conf.socks5_host, &conf.socks5_port);
        }

        else if (name == "ioservice-ipc-port") {
            conf.ioservice_ipcport = boost::lexical_cast<uint16_t>(value);
        }

        else {
            // ignore other args
        }
    }
}


INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();
    common::init_easylogging();

    START_EASYLOGGINGPP(argc, argv);

    MyConfig conf;

    bool found_conf_name = false;
    string found_conf_value;
    vector<pair<string, string> > name_value_pairs;
    auto rv = common::get_cmd_line_name_value_pairs(argc, (const char**)argv,
                                                  found_conf_name, found_conf_value,
                                                  name_value_pairs);
    CHECK(rv == 0);

    if (found_conf_name) {
        name_value_pairs.clear();
        LOG(INFO) << "configuring using config file. other command-line options are ignored.";
        rv = common::get_config_name_value_pairs(found_conf_value.c_str(),
                                                 name_value_pairs);
        CHECK(rv == 0);
    }

    set_my_config(conf, name_value_pairs);

    LOG(INFO) << "io_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    NetConfig netconf;
    if (!conf.socks5_host.empty()) {
        LOG(INFO) << "using socks5 proxy: [" << conf.socks5_host << "]:" << conf.socks5_port;
        CHECK_GT(conf.socks5_port, 0);
        netconf.set_socks5_addr(common::getaddr(conf.socks5_host.c_str()));
        netconf.set_socks5_port(conf.socks5_port);
    }

    myio::TCPServer::UniquePtr tcpServerForIPC(
        new myio::TCPServer(evbase.get(), common::getaddr("localhost"),
                            conf.ioservice_ipcport, nullptr));
    IPCServer::UniquePtr ipcserver(
        new IPCServer(evbase.get(), std::move(tcpServerForIPC), &netconf));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";
    return 0;
}
