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

#include "common.hpp"
#include "easylogging++.h"

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
                CHECK(0) << "error: " << gai_strerror(result);
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

    // we mean to run single-threaded, so we don't need locks
    CHECK_EQ(event_config_set_flag(evconfig.get(), EVENT_BASE_FLAG_NOLOCK), 0);

    struct event_base* evbase = event_base_new_with_config(evconfig.get());
    CHECK_NOTNULL(evbase);

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
    // /* EVLOOP_NO_EXIT_ON_EMPTY not yet available, so we have to
    //  * install a dummy persistent timer to always have some event
    //  */
    // struct timeval tv = {0};
    // tv.tv_sec = 60;
    // tv.tv_usec = 0;
    // struct event *dummy_work_ev = event_new(
    //     evbase, -1, EV_PERSIST | EV_TIMEOUT, [](int, short, void*){
    //         printf("timedout\n");}, nullptr);
    // CHECK_NOTNULL(dummy_work_ev);
    // auto rv = event_add(dummy_work_ev, &tv);
    // CHECK_EQ(rv, 0);

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
   defaultConf.setGlobally(
       el::ConfigurationType::Format,
       "%datetime{%h:%m:%s} %level - %fbase :%line, %func ::   %msg");
   defaultConf.set(
       el::Level::Verbose, el::ConfigurationType::Format,
       "%datetime{%h:%m:%s} %level-%vlevel - %fbase :%line, %func ::   %msg");

    el::Loggers::reconfigureLogger("default", defaultConf);
}

} // end namespace common
