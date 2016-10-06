#ifndef logging_hpp
#define logging_hpp

// these must be defined before including the boost log headers
#define BOOST_LOG_DYN_LINK
#define BOOST_LOG_NO_THREADS // probably will require building
                             // single-thread boost log library


#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/global_logger_storage.hpp>


#define MYLOG(level) BOOST_LOG_SEV(*(mylogging::_my_logger), mylogging::level)

namespace mylogging
{

enum severity_level
{
    INFO = 0,
    WARNING,
    ERROR,
    CRITICAL,
};

extern boost::log::sources::severity_logger< severity_level >* _my_logger;

// default logger: log by severity_level above
// BOOST_LOG_INLINE_GLOBAL_LOGGER_DEFAULT(logger, boost::log::sources::severity_logger<severity_level>)


void setup_boost_logging(const char* procname, const char* level);


}

#endif /* logging_hpp */
