
#include <event2/event.h>
#include <memory>
#include <string.h>
#include <sys/time.h>
#include <boost/lexical_cast.hpp>

#include "../../utility/tcp_server.hpp"
#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "ipc.hpp"

#include "../../experiment_common.hpp"

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

#ifdef IN_SHADOW
    uint16_t tor_socks_port;
    uint16_t tproxy_socks_port;
    std::string browser_proxy_mode_spec_file;
#endif
};

static void
set_my_config(MyConfig& conf,
              const vector<pair<string, string> >& name_value_pairs)
{
    for (auto nv_pair : name_value_pairs) {
        const auto& name = nv_pair.first;
        const auto& value = nv_pair.second;

        if (name == "socks5-hostname") {
#ifdef IN_SHADOW
            LOG(FATAL) << "don't use socks-hostname in shadow";
#else
            common::parse_host_port(value, conf.socks5_host, &conf.socks5_port);
#endif
        }

        else if (name == "ioservice-ipc-port") {
            conf.ioservice_ipcport = boost::lexical_cast<uint16_t>(value);
        }

        else if (name == "tor-socks-port") {
#ifdef IN_SHADOW
            conf.tor_socks_port = boost::lexical_cast<uint16_t>(value);
#else
            LOG(FATAL) << "don't use tor-socks-port outside shadow; use socks5-hostname";
#endif
        }

        else if (name == "tproxy-socks-port") {
#ifdef IN_SHADOW
            conf.tproxy_socks_port = boost::lexical_cast<uint16_t>(value);
#else
            LOG(FATAL) << "don't use tproxy-socks-port outside shadow; use socks5-hostname";
#endif
        }

        else if (name == expcommon::conf_names::browser_proxy_mode_spec_file) {
#ifdef IN_SHADOW
            conf.browser_proxy_mode_spec_file = value;
#else
            LOG(FATAL) << "browser-proxy-mode-spec makes sense only in shadow";
#endif
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

#ifdef IN_SHADOW

    if (!conf.browser_proxy_mode_spec_file.empty()) {
        char myhostname[80] = {0};
        rv = gethostname(myhostname, (sizeof myhostname) - 1);
        CHECK_EQ(rv, 0);

        LOG(INFO) << "my hostname \"" << myhostname << "\"";

        bool found = false;
        const auto proxy_mode = expcommon::get_my_proxy_mode(
            conf.browser_proxy_mode_spec_file.c_str(), myhostname, found);
        CHECK(found) << "cannot find myself in proxy mode spec file";

        LOG(INFO) << "using proxy mode \"" << proxy_mode << "\"";

        conf.socks5_host = "127.0.0.1";
        if ((proxy_mode == expcommon::proxy_mode_tproxy)
            || (proxy_mode == expcommon::proxy_mode_tproxy_via_tor))
        {
            conf.socks5_port = conf.tproxy_socks_port;
        } else if (proxy_mode == expcommon::proxy_mode_tor) {
            conf.socks5_port = conf.tor_socks_port;
        } else if (proxy_mode == expcommon::proxy_mode_none) {
            conf.socks5_host.clear();
            conf.socks5_port = 0;
        } else {
            CHECK(0) << "bad proxy mode \"" << proxy_mode << "\"";
        }
    } else {
        if ((conf.tproxy_socks_port != 0) && (conf.tor_socks_port != 0)) {
            LOG(FATAL) << "cannot specify both tproxy-socks-port and tor-socks-port "
                       << "without browser-proxy-mode-spec; i won't know which proxy to use";
        }

        conf.socks5_host = "127.0.0.1";
        if (conf.tproxy_socks_port) {
            conf.socks5_port = conf.tproxy_socks_port;
        } else if (conf.tor_socks_port) {
            conf.socks5_port = conf.tor_socks_port;
        } else {
            conf.socks5_host.clear();
            conf.socks5_port = 0;
        }
    }

#endif

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
