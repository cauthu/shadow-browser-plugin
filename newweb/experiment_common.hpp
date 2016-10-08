#ifndef EXPERIMENT_COMMON_HPP
#define EXPERIMENT_COMMON_HPP

#include <map>
#include <string>
#include <vector>


namespace expcommon {

static const char proxy_mode_none[] = "none";
static const char proxy_mode_tor[] = "tor";
static const char proxy_mode_tproxy[] = "tproxy";
static const char proxy_mode_tproxy_via_tor[] = "tproxy-via-tor";


namespace conf_names {
static const char browser_proxy_mode_spec_file[] = "browser-proxy-mode-spec-file";
}

std::string
get_my_proxy_mode(const char* modespecfile,
                  const char* myhostname,
                  bool& found);

}

#endif /* end EXPERIMENT_COMMON_HPP */
