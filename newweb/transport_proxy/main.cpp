
#include <fstream>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <event2/event.h>

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


#ifndef IN_SHADOW
static void
s_on_buflo_channel_ready(csp::ClientSideProxy* csp,
                         const bool auto_start_defense_session_on_next_send)
{
    // LOG(INFO) << "buflo channel ready";
    csp->start_accepting_clients();
    LOG(INFO) << "CSP is ready and accepting clients";
    if (auto_start_defense_session_on_next_send) {
        const auto rv = csp->set_auto_start_defense_session_on_next_send();
        CHECK(rv);
        LOG(INFO) << "will automatically start defense session on next send";
    } else {
        LOG(INFO) << "will NOT start any defense session";
    }
}

static void
s_on_SIGUSR2(int, short, void *arg)
{
    auto csp = (csp::ClientSideProxy*)arg;
    CHECK_NOTNULL(csp);

    LOG(INFO) << "received SIGUSR2; request csp to stop defense";
    csp->stop_defense_session(false);
}

static void
s_on_SIGUSR1(int, short, void *arg)
{
    auto csp = (csp::ClientSideProxy*)arg;
    CHECK_NOTNULL(csp);

    LOG(INFO) << "received SIGUSR1; request csp to close all streams";
    csp->close_all_streams();
}

static void
s_on_SIGTERM_SIGINT(int, short, void *arg)
{
    auto csp = (csp::ClientSideProxy*)arg;
    CHECK_NOTNULL(csp);

    LOG(INFO) << "received SIGTERM or SIGINT... logging stats";
    csp->log_stats();

    LOG(INFO) << "exiting now";
    exit(0);
}

#endif


static const char auto_start_defense_session_on_next_send_name[] =
    "auto-start-defense-session-on-next-send";

/* specify a path to file to write (possibly create) a single byte
 * when a defense session is done. will open the file every time we
 * want to write, instead of opening once and writing to the same
 * descriptor.
 */
static const char write_file_on_a_defense_session_done[] =
    "write-file-on-a-defense-session-done";
static const char exit_on_a_defense_session_done[] =
    "exit-on-a-defense-session-done";

static const char tamaraw_packet_interval_name[] =
    "tamaraw-packet-interval";
/* if we are CSP, then will ask SSP to use this packet interval */
static const char ssp_tamaraw_packet_interval_name[] =
    "ssp-tamaraw-packet-interval";
static const char tamaraw_L_name[] =
    "tamaraw-L";
static const char tamaraw_time_limit_secs_name[] =
    "tamaraw-time-limit-secs";


struct MyConfig
{
    MyConfig()
        : ssp_port(common::ports::server_side_transport_proxy)
        , listenport(0)
        , tor_socks_port(0)
        , tproxy_ipcport(common::ports::transport_proxy_ipc)
        , tamaraw_pkt_intvl_ms(0)
        , ssp_tamaraw_pkt_intvl_ms(0)
        , tamaraw_L(0)
        , tamaraw_time_limit_secs(0)
        , ssp_log_outer_connect_latency(false)
#ifndef IN_SHADOW
        , auto_start_defense_session_on_next_send(false)
#endif
    {
    }

    std::string ssp_host;
    uint16_t ssp_port;
    /* for client or sever, depends on is_client bool below */
    uint16_t listenport;
    uint16_t tor_socks_port;
    uint16_t tproxy_ipcport;
    uint16_t tamaraw_pkt_intvl_ms;
    uint16_t ssp_tamaraw_pkt_intvl_ms;
    uint16_t tamaraw_L;
    uint32_t tamaraw_time_limit_secs;
    bool ssp_log_outer_connect_latency;

#ifdef IN_SHADOW
    std::string browser_proxy_mode_spec_file;
#else

    /* automatically start the defense the next time we send stuff to
     * the ssp */
    bool auto_start_defense_session_on_next_send;

    std::shared_ptr<std::string> write_file_on_a_defense_session_done;
    bool exit_on_a_defense_session_done = false;

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

        else if (name == tamaraw_packet_interval_name) {
            try {
                conf.tamaraw_pkt_intvl_ms = boost::lexical_cast<uint16_t>(value);
            }
            catch (...) {
                LOG(FATAL) << "bad value for " << tamaraw_packet_interval_name;
            }
        }

        else if (name == ssp_tamaraw_packet_interval_name) {
            try {
                conf.ssp_tamaraw_pkt_intvl_ms = boost::lexical_cast<uint16_t>(value);
            }
            catch (...) {
                LOG(FATAL) << "bad value for " << ssp_tamaraw_packet_interval_name;
            }
        }

        else if (name == tamaraw_L_name) {
            try {
                conf.tamaraw_L = boost::lexical_cast<uint16_t>(value);
            }
            catch (...) {
                LOG(FATAL) << "bad value for " << tamaraw_L_name;
            }
        }

        else if (name == tamaraw_time_limit_secs_name) {
            try {
                conf.tamaraw_time_limit_secs = boost::lexical_cast<uint32_t>(value);
            }
            catch (...) {
                LOG(FATAL) << "bad value for " << tamaraw_time_limit_secs_name;
            }
        }

        else if (name == "ssp-log-outer-connect-latency") {
            conf.ssp_log_outer_connect_latency = true;
        }

        else if (name == expcommon::conf_names::browser_proxy_mode_spec_file) {
#ifdef IN_SHADOW
            conf.browser_proxy_mode_spec_file = value;
#else
            LOG(FATAL) << "browser-proxy-mode-spec makes sense only in shadow";
#endif
        }

        else if (name == auto_start_defense_session_on_next_send_name) {
#ifdef IN_SHADOW
            LOG(FATAL) << auto_start_defense_session_on_next_send_name
                       << " makes sense only outside shadow";
#else
            CHECK((value == "yes") || (value == "no"))
                << "use yes or no for " << auto_start_defense_session_on_next_send_name;
            conf.auto_start_defense_session_on_next_send = (value == "yes");
#endif
        }

        else if (name == write_file_on_a_defense_session_done) {
#ifdef IN_SHADOW
            LOG(FATAL) << write_file_on_a_defense_session_done
                       << " makes sense only outside shadow";
#else
            CHECK(value.length())
                << write_file_on_a_defense_session_done << " requires non-empty value";
            conf.write_file_on_a_defense_session_done.reset(
                new std::string(value));
#endif
        }

        else if (name == exit_on_a_defense_session_done) {
#ifdef IN_SHADOW
            LOG(FATAL) << exit_on_a_defense_session_done
                       << " makes sense only outside shadow";
#else
            CHECK((value == "yes") || (value == "no"))
                << "use yes or no for " << exit_on_a_defense_session_done;
            conf.exit_on_a_defense_session_done = (value == "yes");
#endif
        }

        else {
            // ignore other args
        }
    }
}

static void
check_tamaraw_params(const MyConfig& conf)
{
    CHECK(conf.tamaraw_pkt_intvl_ms > 0)
        << "need to specify " << tamaraw_packet_interval_name << " to use tamaraw";
    CHECK(conf.tamaraw_L > 0)
        << "need to specify " << tamaraw_L_name << " to use tamaraw";
    CHECK(conf.tamaraw_time_limit_secs > 0)
        << "need to specify " << tamaraw_time_limit_secs_name << " to use tamaraw";

    LOG(INFO) << "tamaraw info:"
              << " packet interval= " << conf.tamaraw_pkt_intvl_ms
              << " , L= " << conf.tamaraw_L
              << " , session time limit= " << conf.tamaraw_time_limit_secs
        ;
}

static void
s_on_buflo_channel_defense_session_done(csp::ClientSideProxy* csp,
                                        const MyConfig& conf)
{

#ifdef IN_SHADOW

#else

    if (conf.write_file_on_a_defense_session_done) {
        std::ofstream ofs(conf.write_file_on_a_defense_session_done->c_str(),
                          std::ofstream::out);
        ofs << "1"; // write a single byte, doesn't matter what the
                    // actual byte value is
        ofs.close();
    }

    if (conf.exit_on_a_defense_session_done) {
        LOG(INFO) << "defense session done... logging stats";
        csp->log_stats();
        LOG(INFO) << "exiting as instructed";
        exit(0);
    }

#endif

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

#ifndef IN_SHADOW

    std::unique_ptr<struct event, void(*)(struct event*)> sigusr1_ev(
        nullptr, event_free);
    std::unique_ptr<struct event, void(*)(struct event*)> sigusr2_ev(
        nullptr, event_free);
    std::unique_ptr<struct event, void(*)(struct event*)> sigterm_ev(
        nullptr, event_free);
    std::unique_ptr<struct event, void(*)(struct event*)> sigint_ev(
        nullptr, event_free);

    /* if either auto start is yes, or any of the tamaraw params is
     * set, then all must be set
     */
    if (conf.auto_start_defense_session_on_next_send ||
        (conf.tamaraw_pkt_intvl_ms || conf.tamaraw_L || conf.tamaraw_time_limit_secs))
    {
        check_tamaraw_params(conf);
    }

#endif

    string proxy_mode = expcommon::proxy_mode_none;
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
            proxy_mode = expcommon::get_my_proxy_mode(
                conf.browser_proxy_mode_spec_file.c_str(), myhostname, found);
            CHECK(found) << "cannot find myself in proxy mode spec file";

            LOG(INFO) << "browser using proxy mode \"" << proxy_mode << "\"";

            do_setup_csp = ((proxy_mode == expcommon::proxy_mode_tproxy)
                            || (proxy_mode == expcommon::proxy_mode_tproxy_via_tor));

            if (!do_setup_csp) {
                LOG(INFO) << "we are of no use; exiting";
                return 0;
            }
        }
        
#endif

        if (do_setup_csp) {
            /* tcpserver to accept connections from clients */
            myio::TCPServer::UniquePtr tcpserver(
                new myio::TCPServer(evbase.get(),
                                    common::getaddr("localhost"),
                                    conf.listenport, nullptr, false));

            LOG(INFO) << "ssp: [" << conf.ssp_host << "]:" << conf.ssp_port;
            if (proxy_mode == expcommon::proxy_mode_tproxy_via_tor) {
                CHECK_GT(conf.tor_socks_port, 0);
            } else if (proxy_mode == expcommon::proxy_mode_tproxy) {
                // force it to zero so we don't use tor
                conf.tor_socks_port = 0;
            }
            LOG(INFO) << "tor socks port: " << conf.tor_socks_port;
            csp.reset(new csp::ClientSideProxy(
                          evbase.get(),
                          std::move(tcpserver),
                          conf.ssp_host.c_str(),
                          conf.ssp_port,
                          conf.tor_socks_port ? common::getaddr("localhost") : 0,
                          conf.tor_socks_port,
                          conf.tamaraw_pkt_intvl_ms,
                          conf.ssp_tamaraw_pkt_intvl_ms,
                          conf.tamaraw_L,
                          conf.tamaraw_time_limit_secs));

            csp->set_a_defense_session_done_cb(
                boost::bind(s_on_buflo_channel_defense_session_done, _1, conf));

            csp->establish_tunnel_2(true);

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
                boost::bind(s_on_buflo_channel_ready, _1,
                            conf.auto_start_defense_session_on_next_send),
                true);

            auto rv2 = 0;

            LOG(INFO) << "setting up SIGUSR1 handler; "
                      << "use SIGUSR1 to close all streams, if any";

            sigusr1_ev.reset(
                evsignal_new(evbase.get(), SIGUSR1, s_on_SIGUSR1, csp.get()));
            CHECK_NOTNULL(sigusr1_ev.get());

            rv2 = event_add(sigusr1_ev.get(), nullptr);
            CHECK_EQ(rv2, 0);

            LOG(INFO) << "setting up SIGUSR2 handler; "
                      << "use SIGUSR2 to stop an active defense, if any";

            sigusr2_ev.reset(
                evsignal_new(evbase.get(), SIGUSR2, s_on_SIGUSR2, csp.get()));
            CHECK_NOTNULL(sigusr2_ev.get());

            rv2 = event_add(sigusr2_ev.get(), nullptr);
            CHECK_EQ(rv2, 0);

            sigterm_ev.reset(
                evsignal_new(evbase.get(), SIGTERM, s_on_SIGTERM_SIGINT, csp.get()));
            CHECK_NOTNULL(sigterm_ev.get());

            rv2 = event_add(sigterm_ev.get(), nullptr);
            CHECK_EQ(rv2, 0);

            sigint_ev.reset(
                evsignal_new(evbase.get(), SIGINT, s_on_SIGTERM_SIGINT, csp.get()));
            CHECK_NOTNULL(sigint_ev.get());

            rv2 = event_add(sigint_ev.get(), nullptr);
            CHECK_EQ(rv2, 0);

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

#else
        
        CHECK(!conf.auto_start_defense_session_on_next_send)
            << "ssp doesn't support " << auto_start_defense_session_on_next_send_name;
        CHECK(!conf.write_file_on_a_defense_session_done)
            << "ssp doesn't support " << write_file_on_a_defense_session_done;
        CHECK(!conf.exit_on_a_defense_session_done)
            << "ssp doesn't support " << exit_on_a_defense_session_done;

#endif

        CHECK(!conf.ssp_tamaraw_pkt_intvl_ms)
            << "ssp doesn't support " << ssp_tamaraw_packet_interval_name;

        /* tcpserver to accept connections from CSPs */
        myio::TCPServer::UniquePtr tcpserver(
            new myio::TCPServer(evbase.get(),
                                INADDR_ANY,
                                conf.listenport, nullptr));

        ssp.reset(new ssp::ServerSideProxy(evbase.get(),
                                           std::move(tcpserver),
                                           conf.tamaraw_pkt_intvl_ms,
                                           conf.tamaraw_L,
                                           conf.tamaraw_time_limit_secs,
                                           conf.ssp_log_outer_connect_latency));
    }

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

#ifdef IN_SHADOW
    CHECK(!do_setup_csp);
#endif
    return 0;
}
