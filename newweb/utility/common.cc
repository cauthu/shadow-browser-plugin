#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event2/event.h>
#include <memory>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "common.hpp"
#include "easylogging++.h"

using std::string;
using std::vector;
using std::make_pair;

namespace common
{

std::string* static_bytes = nullptr;

namespace http
{
const char request_line[] = "GET /special_request HTTP/1.1";
const char request_path[] = "/special_request";
const char resp_meta_size_name[] = "resp-meta-size";
const char resp_body_size_name[] = "resp-body-size";
const char content_length_name[] = "content-length";

const char dummy_name[] = "dummy-header";

const char resp_status_line[] = "HTTP/1.1 200 OK";

const size_t request_line_len = sizeof (request_line) - 1;
const size_t resp_status_line_len = sizeof (resp_status_line) - 1;

} // namespace http

void
init_common()
{
    static_bytes = new std::string(static_bytes_length, 'A');
    CHECK_NOTNULL(static_bytes);
}

/* return true if there's at least a name. the value might be empty */
static bool
maybe_parse_name_value(const std::string& maybe_nv,
                       std::string& name,
                       std::string& value)
{
    if (maybe_nv.find("--") == 0) {
        CHECK(maybe_nv.length() >= 4);
        const auto equal_pos = maybe_nv.find('=');
        if (equal_pos != std::string::npos) {
            name = maybe_nv.substr(2, equal_pos-2);
            value = maybe_nv.substr(equal_pos + 1);
            CHECK(!value.empty()) << "name \"" << name << "\" has empty value";
        } else {
            // no equal sign
            name = maybe_nv.substr(2);
            value = "";
        }
        CHECK(name[0] != '-');

        return true;
    } else {
        return false;
    }
}

/* each line can be empty, a comment (started with #), or a
 * configuration option, with the same format expected as on the
 * command line, i.e.,
 */
int
get_config_name_value_pairs(const char* fpath,
                           std::vector<std::pair<string, string> >& name_value_pairs)
{
    CHECK(name_value_pairs.empty());
    std::ifstream infile(fpath, std::ifstream::in);
    if (!infile.good()) {
        LOG(FATAL) << "error: can't read config file \"" << fpath << "\"";
    }

    string line;
    while (std::getline(infile, line)) {
        if (line.length() == 0 || line.at(0) == '#') {
            /* empty lines and lines beginining with '#' are
               ignored */
            continue;
        }

        string name;
        string value;
        if (maybe_parse_name_value(line, name, value)) {
            name_value_pairs.push_back(make_pair(name, value));
        }
    }

    return 0;
}

/* options can be like:
 *
 * --name
 *
 * --name=value
 *
 * just basic string is supported, no escaping, quoting, etc will be
 * done. same name can appear multiple times; i won't care.
 *
 * if we see a "--conf=...", we will set found_conf_name to true and
 * set found_conf_value to the value
 */
int
get_cmd_line_name_value_pairs(
    int argc,
    const char* argv[],
    bool& found_conf_name,
    std::string& found_conf_value,
    std::vector<std::pair<std::string, std::string> >& name_value_pairs)
{
    CHECK(name_value_pairs.empty());
    for (int i = 0; i < argc; ++i) {
        const string maybe_nv(argv[i]);
        string name;
        string value;
        if (maybe_parse_name_value(maybe_nv, name, value)) {
            name_value_pairs.push_back(make_pair(name, value));

            if (name == "conf") {
                found_conf_name = true;
                found_conf_value = value;
            }
        }
    }
    return 0;
}

void
parse_host_port(const string& host_port_str,
                string& host, uint16_t* port)
{
    vector<string> parts;
    boost::split(parts, host_port_str, boost::is_any_of(":"));
    const auto num_parts = parts.size();
    CHECK((num_parts == 1) || (num_parts == 2));
    if (num_parts == 2) {
        host = parts[0];
        *port = boost::lexical_cast<uint16_t>(parts[1]);
    } else {
        host = parts[0];
    }
}

bool
get_json_doc_from_file(const char* json_fpath,
                       rapidjson::Document& doc)
{
    std::fstream fs(json_fpath, std::ios_base::in);
    if (!fs.is_open()) {
        LOG(FATAL) << "unable to open json file at \"" << json_fpath << "\"";
    }
    std::stringstream ss;
    ss << fs.rdbuf();
    fs.close();

    const std::string file_contents = ss.str();
    doc.Parse(file_contents.c_str());

    return true;
}

char*
expandPath(const char* path) {
    char *s = NULL;
    if (path[0] == '~') {
        struct passwd *pw = getpwuid(getuid());
        const char *homedir = pw->pw_dir;
        CHECK_GT(asprintf(&s, "%s%s", homedir, path+1), 0);
    } else {
        s = strdup(path);
    }
    return s;
}

uint64_t
gettimeofdayMs(struct timeval* t)
{
    struct timeval now;
    if (NULL == t) {
        CHECK_EQ(gettimeofday(&now, NULL), 0);
        t = &now;
    }
    return (((uint64_t)t->tv_sec) * 1000) + (uint64_t)floor(((double)t->tv_usec) / 1000);
}

void
printhex(const char *hdr,
         const unsigned char *md_value,
         unsigned int md_len)
{
    fprintf(stderr, "%s: printing hex: [", hdr);
    for(int i = 0; i < md_len; i++) {
        fprintf(stderr, "%02x", md_value[i]);
    }
    fprintf(stderr,"]\n");
}

void
to_hex(const unsigned char *value,
       unsigned int len,
       char *hex)
{
    for(int i = 0; i < len; i++) {
        sprintf(&(hex[i*2]), "%02x", value[i]);
    }
}

in_addr_t
getaddr(const char *hostname)
{
    if (!hostname || 0 == strlen(hostname)) {
        return htonl(INADDR_NONE);
    }
    /* check if we have an address as a string */
    struct in_addr in;
    int is_ip_address = inet_aton(hostname, &in);

    if(is_ip_address) {
        return in.s_addr;
    } else {
        in_addr_t addr = 0;

        /* get the address in network order */
        if(strcmp(hostname, "none") == 0) {
            addr = htonl(INADDR_NONE);
        } else if(strcmp(hostname, "localhost") == 0) {
            addr = htonl(INADDR_LOOPBACK);
        } else {
            struct addrinfo hints;
            memset(&hints, 0, sizeof hints); // make sure the struct is empty
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
            hints.ai_flags = AI_PASSIVE;     // fill in my IP for me

            struct addrinfo* info;
            int result = getaddrinfo(hostname, nullptr, &hints, &info);
            if(result != 0) {
                VLOG(1) << "error: " << gai_strerror(result);
                return INADDR_NONE;
            }

            addr = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
            freeaddrinfo(info);
        }

        return addr;
    }
}

struct event_base*
init_evbase()
{
    std::unique_ptr<struct event_config, void(*)(struct event_config*)> evconfig(
        event_config_new(), event_config_free);
    CHECK_NOTNULL(evconfig);

    /* I'm going to have events running at two priorities: high
     * priority (0) probably exclusively used by the buflo defense
     * timers; everything else uses the low priority (1)
     *
     * "When you do not set the priority for an event, the default is
     * the number of queues in the event base, divided by 2."
     *
     * which, if we use 2-levels, then default is 2/2=1 is the low
     * priority, and that's what we want.
     */

#if 0
    /* we want to use the same libevent that tor plugin uses, just to
     * avoid unexpected issues. that version 2.0.21-stable doesn't
     * have event_config_set_max_dispatch_interval() so we can't use it.
     *
     * we might not need it, but we should check in the buflo timer
     * fired callback how far off schedule it is and log WARNING/ERROR
     * if it accumulates too much delays.
     */

    // check for high priority events every 10 msg (cuz for now the
    // most frequent buflo timer we support is 10ms)
    struct timeval msec_10 = { 0, 10*1000 };
    auto rv = event_config_set_max_dispatch_interval(
        evconfig.get(), &msec_10, 5, 1);
    CHECK_EQ(rv, 0);
#endif

    // we mean to run single-threaded, so we don't need locks
    CHECK_EQ(event_config_set_flag(evconfig.get(), EVENT_BASE_FLAG_NOLOCK), 0);

    struct event_base* evbase = event_base_new_with_config(evconfig.get());
    CHECK_NOTNULL(evbase);

    /* available starting in 2.1.1-alpha */
    // rv = event_base_get_npriorities(evbase);
    // CHECK_EQ(rv, 1);

    auto rv = event_base_priority_init(evbase, 2);
    CHECK_EQ(rv, 0);

    // rv = event_base_get_npriorities(evbase);
    // CHECK_EQ(rv, 2);

    /* double check a few things */
    auto chosen_method = event_base_get_method(evbase);
    if (strcmp("epoll", chosen_method)) {
        printf("ERROR: libevent is using \"%s\"; we want \"epoll.\"\n", chosen_method);
        exit(1);
    }

    auto features = event_base_get_features(evbase);
    if (! (features & EVENT_BASE_FLAG_NOLOCK)) {
        printf("ERROR: libevent is using locks; we do not want that.\n");
        exit(1);
    }

    return evbase;
}

void
dispatch_evbase(struct event_base* evbase)
{
    // /******  run the loop until told to stop ******/
    // /* only add timeout event to double-check the default priority is
    //  * 1, since we are only levels of priorities.
    //  */
    // struct timeval tv = {0};
    // tv.tv_sec = 3600;
    // tv.tv_usec = 0;
    // struct event *dummy_work_ev = event_new(
    //     evbase, -1, EV_PERSIST | EV_TIMEOUT, [](int, short, void*){
    //         printf("timedout\n");}, nullptr);
    // CHECK_NOTNULL(dummy_work_ev);
    // auto rv = event_add(dummy_work_ev, &tv);
    // CHECK_EQ(rv, 0);

    /* available in 2.1.1-alpha */
    // const auto priority = event_get_priority(dummy_work_ev);
    // CHECK_EQ(priority, 1);

    event_base_dispatch(evbase);

    // if (dummy_work_ev) {
    //     event_free(dummy_work_ev);
    // }
}

void
init_easylogging()
{
#ifdef IN_SHADOW
    CHECK_EQ(el::base::elStorage.get(), nullptr);
    // theses lines are supposed to be done by
    // INITIALIZE_EASYLOGGINGPP macro, but somehow in shadow they
    // don't get to run, so we manually set them here
    el::base::elStorage.reset(
        new el::base::Storage(el::LogBuilderPtr(new el::base::DefaultLogBuilder())));
    // el::base::utils::s_currentUser = el::base::utils::OS::currentUser();
    // el::base::utils::s_currentHost = el::base::utils::OS::currentHost();
    el::base::utils::s_termSupportsColor = el::base::utils::OS::termSupportsColor();
#endif

   el::Configurations defaultConf;
   defaultConf.setToDefault();

#ifdef IN_SHADOW

   defaultConf.setGlobally(
       el::ConfigurationType::Format,
       "%datetime{%h:%m:%s} %level - %fbase :%line, %func ::   %msg");
   defaultConf.set(
       el::Level::Verbose, el::ConfigurationType::Format,
       "%datetime{%h:%m:%s} %level-%vlevel - %fbase :%line, %func ::   %msg");

#else

   defaultConf.setGlobally(
       el::ConfigurationType::Format,
       "%datetime %level - %fbase :%line, %func ::   %msg");
   defaultConf.set(
       el::Level::Verbose, el::ConfigurationType::Format,
       "%datetime %level-%vlevel - %fbase :%line, %func ::   %msg");

#endif

    // by default easylogging also logs to a default log file
    // "logs/myeasylog.log" every loggable message; we disable that
    // behavior here... god this was driving me crazy for half a day
    // because i'd set -DELPP_NO_DEFAULT_LOG_FILE thinking that would
    // do the job, but it only caused easylogging to crash when it
    // tries to write to the file (on line easylogging++.h:4235) but
    // the fs is garbage
    //
    // fs->write(logLine.c_str(), logLine.size());
    //
    defaultConf.setGlobally(el::ConfigurationType::ToFile, "false");

    const std::string identity("default");
    el::Loggers::reconfigureLogger(identity, defaultConf);
}

} // end namespace common
