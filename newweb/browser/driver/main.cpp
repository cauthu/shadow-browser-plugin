
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

#include "../../experiment_common.hpp"

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

    struct {
        bool found;
        std::string path;
    } page_models_list_file;

#ifdef IN_SHADOW
    std::string browser_proxy_mode_spec_file;
#endif

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

        else if (name == "page-models-list-file") {
            conf.page_models_list_file.found = true;
            conf.page_models_list_file.path = value;
        }

//         else if (name == "load-page-then-exit") {
// #ifdef IN_SHADOW
//             LOG(FATAL) << "load-page-then-exit does not yet make sense in shadow";
// #else
//             conf.load_model_then_exit.found = true;
//             conf.load_model_then_exit.path = value;
// #endif
//         }

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

    string proxy_mode = expcommon::proxy_mode_none;

#ifdef IN_SHADOW

    if (!conf.browser_proxy_mode_spec_file.empty()) {
        char myhostname[80] = {0};
        rv = gethostname(myhostname, (sizeof myhostname) - 1);
        CHECK_EQ(rv, 0);

        LOG(INFO) << "my hostname \"" << myhostname << "\"";

        bool found = false;
        proxy_mode = expcommon::get_my_proxy_mode(
            conf.browser_proxy_mode_spec_file.c_str(), myhostname, found);
        CHECK(found) << "cannot find myself in proxy mode spec file";

        LOG(INFO) << "using proxy mode \"" << proxy_mode << "\"";

        if ((proxy_mode != expcommon::proxy_mode_tproxy)
            && (proxy_mode != expcommon::proxy_mode_tproxy_via_tor))
        {
            // let's NOT use the tproxy
            conf.tproxy_ipcport = 0;
        }
    }

#else

    // if (!conf.load_model_then_exit.found) {
    //     LOG(FATAL) << "must specify page model to load with --load-page-then-exit";
    // }

#endif

    if (!conf.page_models_list_file.found) {
        LOG(FATAL) << "must specify page models list file";
    }

    LOG(INFO) << "driver_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    Driver::UniquePtr driver(
        new Driver(evbase.get(), conf.page_models_list_file.path,
                   proxy_mode,
                   conf.tproxy_ipcport, conf.renderer_ipcport));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";

    return 0;
}
