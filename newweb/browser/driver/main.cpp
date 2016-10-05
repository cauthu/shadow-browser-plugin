
#include <event2/event.h>
#include <memory>
#include <arpa/inet.h>
#include <boost/lexical_cast.hpp>
#include <vector>
#include <string>

#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/tcp_channel.hpp"
#include "driver.hpp"


using std::unique_ptr;
using std::vector;
using std::pair;
using std::string;

struct MyConfig
{
    MyConfig()
        : renderer_ipcport(common::ports::default_renderer_ipc)
          /* by default we don't control tproxy */
        , tproxy_ipcport(0)
    {
    }

    uint16_t renderer_ipcport;
    uint16_t tproxy_ipcport;
};

void
set_my_config(MyConfig& conf,
              const vector<pair<string, string> >& name_value_pairs)
{
    for (auto nv_pair : name_value_pairs) {
        const auto& name = nv_pair.first;
        const auto& value = nv_pair.second;

        if (name == "renderer-ipc-port") {
            conf.renderer_ipcport = boost::lexical_cast<uint16_t>(value);
        }

        else if (name == "tproxy-ipc-port") {
            conf.tproxy_ipcport = boost::lexical_cast<uint16_t>(value);
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

    LOG(INFO) << "driver_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    Driver::UniquePtr driver(
        new Driver(evbase.get(), conf.tproxy_ipcport, conf.renderer_ipcport));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";

    return 0;
}
