
#include <event2/event.h>
#include <memory>
#include <arpa/inet.h>
#include <boost/lexical_cast.hpp>

#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/tcp_channel.hpp"
#include "driver.hpp"


using std::unique_ptr;

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv)
{
    common::init_common();

    common::init_easylogging();

    uint16_t renderer_ipcport = common::ports::default_renderer_ipc;
    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--rendererIpcPort")) {
            renderer_ipcport = boost::lexical_cast<uint16_t>(argv[i+1]);
        }
    }

    CHECK_GT(renderer_ipcport, 0);

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "driver_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    Driver::UniquePtr driver(
        new Driver(evbase.get(), common::ports::transport_proxy_ipc,
                   renderer_ipcport));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";

    return 0;
}
