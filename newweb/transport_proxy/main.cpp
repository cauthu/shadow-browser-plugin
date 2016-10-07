
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>

#include "../utility/tcp_server.hpp"
#include "../utility/common.hpp"
#include "../utility/easylogging++.h"

#include "ipc.hpp"
#include "csp/csp.hpp"
#include "ssp/ssp.hpp"

#include "../experiment_common.hpp"


using std::vector;
using std::pair;
using std::string;

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
s_on_buflo_channel_ready(csp::ClientSideProxy* csp)
{
    LOG(INFO) << "buflo channel ready";
    csp->start_accepting_clients();
    LOG(INFO) << "CSP is ready and accepting clients";
}

struct MyConfig
{
    MyConfig()
        : ssp_port(common::ports::server_side_transport_proxy)
        , listenport(0)
        , tor_socks_port(0)
        , tproxy_ipcport(common::ports::transport_proxy_ipc)
        , tamaraw_pkt_intvl_ms(0)
        , tamaraw_L(0)
    {
    }

    std::string ssp_host;
    uint16_t ssp_port;
    /* for client or sever, depends on is_client bool below */
    uint16_t listenport;
    uint16_t tor_socks_port;
    uint16_t tproxy_ipcport;
    uint16_t tamaraw_pkt_intvl_ms;
    uint16_t tamaraw_L;

#ifdef IN_SHADOW
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

        if (name == "ssp") {
            // should be host:port
            /* NOTE that due to issue
             * https://bitbucket.org/hatswitch/shadow-plugin-extras/issues/3/
             * the csp will do local lookup of the ssp's ip address
             * and give that to Tor proxy
             */
            common::parse_host_port(value, conf.ssp_host, &conf.ssp_port);
        }

        else if (name == "port") {
            conf.listenport = boost::lexical_cast<uint16_t>(value);
        }

        else if (name == "tor-socks-port") {
            /* will connect to local (i.e., "localhost") tor proxy at
             * this port */
            conf.tor_socks_port = boost::lexical_cast<uint16_t>(value);
        }

        else if (name == "tproxy-ipc-port") {
            conf.tproxy_ipcport = boost::lexical_cast<uint16_t>(value);
        }

        else if (name == "tamaraw-packet-interval") {
            conf.tamaraw_pkt_intvl_ms = boost::lexical_cast<uint16_t>(value);
        }

        else if (name == "tamaraw-L") {
            conf.tamaraw_L = boost::lexical_cast<uint16_t>(value);
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

    // have to manually parse command line args because shadow
    // confuses getopt[_long] (or at least it used to the last time i
    // tried it)

    // either pass configuration options via command line, or put them
    // in a config file and use "--conf"
    //
    // do NOT mix both approaches. result will be undefined

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

    const bool is_client = !conf.ssp_host.empty();

    LOG(INFO) << "TransportProxy starting...";

    std::unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    if (conf.listenport == 0) {
        conf.listenport = is_client
                     ? common::ports::client_side_transport_proxy
                     : common::ports::server_side_transport_proxy;
    }

    LOG(INFO) << (is_client ? "client-" : "server-")
              << "side proxy, will accept clients on port " << conf.listenport;

    /* ***************************************** */

    csp::ClientSideProxy::UniquePtr csp;
    ssp::ServerSideProxy::UniquePtr ssp;

    myio::TCPServer::UniquePtr tcpServerForIPC;
    IPCServer::UniquePtr ipcserver;

    bool do_setup_csp = true;
    if (is_client) {
#ifdef IN_SHADOW
        // find out if this node does not use buflo tproxy

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

            do_setup_csp = (proxy_mode == expcommon::proxy_mode_tproxy);
        }
#endif

        if (do_setup_csp) {
            /* tcpserver to accept connections from clients */
            myio::TCPServer::UniquePtr tcpserver(
                new myio::TCPServer(evbase.get(),
                                    common::getaddr("localhost"),
                                    conf.listenport, nullptr, false));

            VLOG(2) << "ssp: [" << conf.ssp_host << "]:" << conf.ssp_port;
            VLOG(2) << "tor_socks_port: " << conf.tor_socks_port;
            csp.reset(new csp::ClientSideProxy(
                          evbase.get(),
                          std::move(tcpserver),
                          conf.ssp_host.c_str(),
                          conf.ssp_port,
                          conf.tor_socks_port ? common::getaddr("localhost") : 0,
                          conf.tor_socks_port,
                          conf.tamaraw_pkt_intvl_ms,
                          conf.tamaraw_L));

#ifdef IN_SHADOW
            // only in shadow do we use ipc cuz shadow doesn't support
            // launching nodes (i.e., fork() so we can't restart csp
            // for every page load, so we need to use ipc
            //
            // outside shadow, we just kill and launch csp every time,
            // so don't need ipc

            LOG(INFO) << "ipc server listens on " << conf.tproxy_ipcport;
            tcpServerForIPC.reset(
                new myio::TCPServer(
                    evbase.get(), common::getaddr("localhost"),
                    conf.tproxy_ipcport, nullptr));
            ipcserver.reset(
                new IPCServer(
                    evbase.get(), std::move(tcpServerForIPC), std::move(csp)));
#else
            const auto rv = csp->establish_tunnel(
                boost::bind(s_on_buflo_channel_ready, _1),
                true);
#endif

        } else {
#ifdef IN_SHADOW
            // don't do anything
#else
            // if not setting up csp outside shadow then it's a bug
            LOG(FATAL) << "bug";
#endif
        }

    } else {

#ifdef IN_SHADOW
        if (!conf.browser_proxy_mode_spec_file.empty()) {
            LOG(FATAL) << "tproxy ssp does not use browser proxy mode spec file";
        }
#endif

        /* tcpserver to accept connections from CSPs */
        myio::TCPServer::UniquePtr tcpserver(
            new myio::TCPServer(evbase.get(),
                                INADDR_ANY,
                                conf.listenport, nullptr));

        ssp.reset(new ssp::ServerSideProxy(evbase.get(),
                                           std::move(tcpserver),
                                           conf.tamaraw_pkt_intvl_ms,
                                           conf.tamaraw_L));
    }

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

#ifdef IN_SHADOW
    CHECK(!do_setup_csp);
#endif
    return 0;
}
