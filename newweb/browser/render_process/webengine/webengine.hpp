#ifndef webengine_hpp
#define webengine_hpp

#include <memory>

#include "../../../utility/object.hpp"

#include "../interfaces.hpp"
#include "../ipc_io_service.hpp"

#include "page_model.hpp"

#include "dom/Document.hpp"
#include "fetch/Resource.hpp"
#include "fetch/ResourceFetcher.hpp"



namespace blink
{

class Webengine : public Object
                , public ResourceMsgHandler
{
public:
    typedef std::unique_ptr<Webengine, /*folly::*/Destructor> UniquePtr;

    explicit Webengine(IOServiceIPCClient*);

    /* takes a file path to the page model */
    void loadPage(const char* model_fpath);

    /* will send a request to the io process, and will notify the
     * Resource response/data
     */
    void request_resource(const PageModel::RequestInfo& req_info,
                          Resource* res);

    /* implement ResourceMsgHandler interface */
    virtual void handle_ReceivedResponse(const int& req_id) override;
    virtual void handle_DataReceived(const int& req_id, const size_t& length) override;
    virtual void handle_RequestComplete(const int& req_id, const bool success) override;

    void msleep(const uint32_t ms);

    void execute_scope(const uint32_t scope_id);

protected:

    virtual ~Webengine() = default;

    //////

    void _init_angelscript_engine();

    /////

    IOServiceIPCClient* ioservice_ipcclient_;

    /* map from the request id (for IPC!! not the resInstNum) that we
     * generate to the resource for which we're requesting. this is
     * like chrome's resource_disptacher's "pending_requests_"
     */
    std::map<int, Resource*> pending_requests_;

    PageModel::UniquePtr page_model_;
    ResourceFetcher::UniquePtr resource_fetcher_;
    Document::UniquePtr document_;

    /* these are ids of the execution scopes that have executed, and
     * therefore cannot be executed again. we will crash on such
     * attempt
     */
    std::set<uint32_t> executed_scope_ids_;
};

}

#endif /* webengine_hpp */
