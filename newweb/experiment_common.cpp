

#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>

#include "utility/easylogging++.h"

#include "experiment_common.hpp"

using std::string;
using std::vector;

namespace expcommon {

std::string
get_my_proxy_mode(const char* modespecfile,
                  const char* myhostname,
                  bool& found)
{
    /*
     * each line is like:
     *
     * hostname = tor|tproxy|none
     *
     * this function looks for the entry with my host name, and return
     * the corresponding value
     *
     */

    CHECK_NOTNULL(modespecfile);
    LOG(INFO) << "Using proxy mode spec file \"" << modespecfile << "\"";
    std::ifstream infile(modespecfile, std::ifstream::in);
    if (!infile.good()) {
        LOG(FATAL) << "error: can't read proxy mode spec file";
    }
    string line;
    found = false;
    while (std::getline(infile, line)) {
        VLOG(1) << "line: [" << line << "]";
        if (line.length() == 0 || line.at(0) == '#') {
            /* empty lines and lines beginining with '#' are
               ignored */
            continue;
        }

        if (boost::starts_with(line.c_str(), myhostname)) {
            std::istringstream iss(line);
            string token;
            std::getline(iss, token, '='); // get rid of hostname
            std::getline(iss, token, '='); // now get the mode
            boost::algorithm::trim(token);
            CHECK(token.length() > 0);
            VLOG(1) << "mode: [" << token << "]";
            const auto& mode = token;
            if ((mode != proxy_mode_none)
                && (mode != proxy_mode_tor)
                && (mode != proxy_mode_tproxy)
                && (mode != proxy_mode_tproxy_via_tor))
            {
                LOG(FATAL) << "invalid browser proxy mode \"" << mode << "\"";
            }

            found = true;
            return mode;
        }
    }

    return std::string();
}

} // end namespace expcommon
