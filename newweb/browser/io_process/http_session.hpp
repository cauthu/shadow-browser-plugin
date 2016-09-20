#ifndef http_session_hpp
#define http_session_hpp


/* maybe kinda like chrome's HttpNetworkSession
 *
 * essentially represents information/state for ONE browser, e.g.:
 *
 * - contains/maintains a connection/network manager/pool
 *
 * - submits requests to the connection manager, etc.
 *
 * - in future might deal with caching
 *
 * - can serve multiple renderers/pages at the same time (just like a
 *   single chrome browser), using the same connection pool, etc.
 *
 * if something like ssp, that want to isolate concurrent clients,
 * then use different instances of http sessions
 *
 */


#include <memory>
#include <map>

#include "../../utility/object.hpp"
#include "../../utility/http/request.hpp"
#include "../../utility/http/connection_manager.hpp"
// #include "utility/ipc/io_service/gen/combined_headers"

#include "ipc.hpp"
#include "net_config.hpp"


class HttpNetworkSession : public Object
{
public:
    typedef std::unique_ptr<HttpNetworkSession, /*folly::*/Destructor> UniquePtr;

    explicit HttpNetworkSession(struct event_base*,
                                IPCServer*,
                                uint32_t routing_id,
                                const NetConfig*);

    void handle_RequestResource(const int req_res_req_id,
                                           const char* host,
                                           const uint16_t port,
                                           const size_t req_total_size,
                                           const size_t resp_meta_size,
                                const size_t resp_body_size);

private:

    virtual ~HttpNetworkSession() = default;

    void _response_meta_cb(const int& status, char **headers,
                           http::Request* req);
    void _response_body_data_cb(const uint8_t *data, const size_t& len,
                                http::Request* req);
    void _response_done_cb(http::Request* req, bool success);

    /////////////

    struct event_base* evbase_; // don't free

    IPCServer* ipcserver_; // don't free
    // given by ipc server for us to use when telling ipc server to
    // send message
    const uint32_t routing_id_;
    const NetConfig* netconf_; // don't free

    http::ConnectionManager::UniquePtr connman_;

    struct PendingRequestInfo
    {
        int req_res_req_id; /* from the requestresource msg */
        std::shared_ptr<http::Request> req;
    };
    std::map<uint32_t, PendingRequestInfo > pending_requests_;
};

#endif /* end http_session_hpp */
