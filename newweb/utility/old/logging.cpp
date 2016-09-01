
#include <stdio.h>
#include <typeinfo>
#include <string>
#include <iostream>
#include <map>
#include <sys/types.h>
#include <unistd.h>

#include "logging.hpp"

#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/unlocked_frontend.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/attributes/clock.hpp>
#include <boost/log/attributes/current_process_name.hpp>
#include <boost/log/attributes/current_process_id.hpp>
#include <boost/log/support/date_time.hpp>

#include "myassert.h"

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
namespace attrs = boost::log::attributes;


namespace mylogging
{

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", severity_level)
BOOST_LOG_ATTRIBUTE_KEYWORD(timestamp, "Timestamp", boost::posix_time::ptime)
BOOST_LOG_ATTRIBUTE_KEYWORD(my_process_name, "MyProcessName", const char*)

static const char* severity_level_strings[] = {
    "INFO",
    "WARNING",
    "ERROR",
    "CRITICAL",
    NULL,
};

// A trivial sink backend that requires no thread synchronization
class stdout_backend :
        public sinks::basic_sink_backend< sinks::concurrent_feeding >
{
public:

    // The function is called for every log record to be written to log
    void consume(logging::record_view const& rec)
    {
        auto level_attr_val = rec[severity];
        myassert(level_attr_val);
        const auto level_str = severity_level_strings[rec[severity].get()];
        auto procname_attr_val = rec[my_process_name];
        myassert(procname_attr_val);
        // const auto pid = 12;
        const auto procname = procname_attr_val.get();
        // some reason std::endl doesn't work
        printf("== [%s] _ [%s]: %s\n", procname, level_str, rec[expr::smessage].get().c_str());
    }
};

// Complete sink type
typedef sinks::unlocked_sink< stdout_backend > stdout_sink_t;

boost::shared_ptr<logging::core> g_core;
// logging::core* g_core = nullptr;

src::severity_logger<severity_level>* _my_logger = nullptr;

void
setup_boost_logging(const char* procname, const char *level)
{
    enum severity_level min_sev_level = WARNING;
    if (level) {
        if (!strcmp(level, "INFO")) min_sev_level = INFO;
        else if (!strcmp(level, "WARNING")) min_sev_level = WARNING;
        else if (!strcmp(level, "ERROR")) min_sev_level = ERROR;
        else if (!strcmp(level, "CRITICAL")) min_sev_level = CRITICAL;
        else {
            myassert(0); // invalid level
        }
    }

    /* getpid() returns the pid of shadow simulator instead of the
     * simulated processes */
    // const pid_t pid = getpid();

    char* p = (char*)malloc(123);
    // add global attributes
    _my_logger = new src::severity_logger<severity_level>();

    g_core = logging::core::get();
    printf("_____ %s ==== %s ===== %p [[  %p ]] (( %p ))\n",
           procname, level, g_core.get(), p, _my_logger);
    g_core->add_global_attribute("TimeStamp", attrs::local_clock());
    g_core->add_global_attribute("Process", attrs::current_process_name());
    // g_core->add_global_attribute("ProcessID", attrs::current_process_id());
    g_core->add_global_attribute("MyProcessName", attrs::constant<const char*>(procname));

    // Set a global filter so that only error messages are logged
    g_core->set_filter(severity >= min_sev_level);


    // create and regisiter stdout sink
    boost::shared_ptr<stdout_backend> backend = boost::make_shared<stdout_backend>();
    boost::shared_ptr<stdout_sink_t> stdout_sink = boost::make_shared<stdout_sink_t>(backend);

    g_core->add_sink(stdout_sink);

    MYLOG(INFO) << "ipc server got new client stream ";

    MYLOG(INFO) << "A regular message";
    MYLOG(WARNING) << "Something bad is going on but I can handle it";
    MYLOG(CRITICAL) << "Everything crumbles, shoot me now!";

    // // You can manage filtering through the sink interface
    // stdout_sink->set_filter(severity >= WARNING);
}

}
