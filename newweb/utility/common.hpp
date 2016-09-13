#ifndef COMMON_HPP
#define COMMON_HPP

#include <stdio.h>
#include <sys/time.h>
#include <math.h>
#include <stdint.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>
#include <netinet/in.h>
#include <event2/event.h>
#include <type_traits>
#include <string>


namespace common
{

/* need to call this to initialize some static vars. otherwise shadow
 * will crash because it doesn't seem to deal with static vars
 * properly, e.g., std::string vars like "static_bytes" below
 */
void init_common();

/* 16K */
const size_t static_bytes_length = 0xffff;
extern std::string* static_bytes;

namespace http
{
extern const char request_line[];
extern const char request_path[];
extern const char resp_meta_size_name[];
extern const char resp_body_size_name[];
extern const char content_length_name[];

extern const char dummy_name[];

extern const char resp_status_line[];

extern const size_t request_line_len;
extern const size_t resp_status_line_len;

} // namespace http

namespace ports
{

const uint16_t client_side_transport_proxy = 8080;
const uint16_t server_side_transport_proxy = 8081;

} // namespace ports

/* if "path" begins with '~', then replace '~' with the homedir path
 * of the user.
 *
 * caller is responsible for freeing the returned memory.
 */
char*
expandPath(const char* path);

uint64_t
gettimeofdayMs(struct timeval* t);

void
printhex(const char *hdr,
         const unsigned char *md_value,
         unsigned int md_len);

void
to_hex(const unsigned char *value,
       unsigned int len,
       char *hex);

in_addr_t
getaddr(const char *hostname);

struct event_base*
init_evbase();

void
dispatch_evbase(struct event_base*);

void
init_easylogging();

template <typename Enumeration>
auto as_integer(Enumeration const value)
    -> typename std::underlying_type<Enumeration>::type
{
    return static_cast<typename std::underlying_type<Enumeration>::type>(value);
}

} // end namespace common


#ifdef ENABLE_MY_LOG_MACROS
#define logDEBUG(fmt, ...)                                              \
    do {                                                                \
        logfn(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "line %d: " fmt,         \
              __LINE__, ##__VA_ARGS__);                                 \
    } while (0)

#define logINFO(fmt, ...)                                               \
    do {                                                                \
        logfn(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "line %d: " fmt,          \
              __LINE__, ##__VA_ARGS__);                                 \
    } while (0)

#define logMESSAGE(fmt, ...)                                            \
    do {                                                                \
        logfn(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "line %d: " fmt,       \
              __LINE__, ##__VA_ARGS__);                                 \
    } while (0)

#define logWARN(fmt, ...)                                               \
    do {                                                                \
        logfn(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "line %d: " fmt,       \
              __LINE__, ##__VA_ARGS__);                                 \
    } while (0)

#define logCRITICAL(fmt, ...)                                           \
    do {                                                                \
        logfn(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "line %d: " fmt,      \
              __LINE__, ##__VA_ARGS__);                                 \
    } while (0)

#else

/* no ops */
#define logDEBUG(fmt, ...)

#define logINFO(fmt, ...)

#define logMESSAGE(fmt, ...)

#define logWARN(fmt, ...)

#define logCRITICAL(fmt, ...)

#endif

#define ARRAY_LEN(arr)  (sizeof(arr) / sizeof((arr)[0]))

// template<typename T1, typename T2>
// inline bool
// inMap(const std::map<T1, T2>& m, const T1& k)
// {
//     return m.end() != m.find(k);
// }

#define inMap(m, k)                             \
    ((m).end() != (m).find(k))

#define inSet(s, k)                             \
    ((s).end() != (s).find(k))

#endif /* COMMON_HPP */
