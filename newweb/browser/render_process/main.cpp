
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



using std::unique_ptr;
using std::vector;
using std::string;
using std::pair;

struct MyConfig
{
    MyConfig()
        : renderer_ipcport(common::ports::default_renderer_ipc)
        , ioservice_ipcport(common::ports::io_service_ipc)
    {
    }

    uint16_t renderer_ipcport;
    uint16_t ioservice_ipcport;
};

static void
set_my_config(MyConfig& conf,
              const vector<pair<string, string> >& name_value_pairs)
{
    for (auto nv_pair : name_value_pairs) {
        const auto& name = nv_pair.first;
        const auto& value = nv_pair.second;

        if (name == "renderer-ipc-port") {
            conf.renderer_ipcport = boost::lexical_cast<uint16_t>(value);
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

static void
s_on_io_service_ipc_client_status(IOServiceIPCClient::ChannelStatus status,
                                  struct event_base* evbase,
                                  uint16_t renderer_ipcport)
{
    CHECK_EQ(status, IOServiceIPCClient::ChannelStatus::READY);

    LOG(INFO) << "ioservice ipc client is ready";

    /// set up my ipc server
    myio::TCPServer::UniquePtr tcpServerForIPC;

    LOG(INFO) << "my ipc server listens on " << renderer_ipcport;
    tcpServerForIPC.reset(
        new myio::TCPServer(
            evbase, common::getaddr("localhost"),
            renderer_ipcport, nullptr));
    ipcserver.reset(new IPCServer(evbase, std::move(tcpServerForIPC)));

    webengine.reset(
        new blink::Webengine(evbase,
                             io_service_ipc_client.get(),
                             ipcserver.get()));

    VLOG(2) << "ioservice ip client: " << io_service_ipc_client.get()
            << " , my ipcserver: " <<  ipcserver.get();
}

/*==================================================*/


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

    CHECK_GT(conf.renderer_ipcport, 0)
        << "must specify a positive port to listen on to provide renderer ipc service";

    LOG(INFO) << "render_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */


    LOG(INFO) << "use ioservice on ipc port " << conf.ioservice_ipcport;
    LOG(INFO) << "renderer ipc server listens on " << conf.renderer_ipcport;

    myio::TCPChannel::UniquePtr tcpch1(
        new myio::TCPChannel(evbase.get(), common::getaddr("localhost"),
                             conf.ioservice_ipcport, nullptr));

    io_service_ipc_client.reset(
        new IOServiceIPCClient(
            evbase.get(), std::move(tcpch1),
            boost::bind(s_on_io_service_ipc_client_status,
                        _2, evbase.get(), conf.renderer_ipcport)));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";

    return 0;
}
