
#include <event2/event.h>
#include <memory>
#include <arpa/inet.h>

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

    START_EASYLOGGINGPP(argc, argv);

    LOG(INFO) << "driver_process starting...";

    unique_ptr<struct event_base, void(*)(struct event_base*)> evbase(
        common::init_evbase(), event_base_free);

    /* ***************************************** */

    Driver::UniquePtr driver(
        new Driver(evbase.get(), common::ports::transport_proxy_ipc));

    /* ***************************************** */

    LOG(INFO) << "done setup. run event loop";

    common::dispatch_evbase(evbase.get());

    LOG(FATAL) << "not reached";

    return 0;
}
