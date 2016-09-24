#ifndef webengine_hpp
#define webengine_hpp


#include "../../../utility/object.hpp"

#include "../interfaces.hpp"
#include "../ipc_io_service.hpp"

#include "fetch/Resource.hpp"



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

    /* implement ResourceMsgHandler interface */
    virtual void handle_ReceivedResponse(const int& req_id) override;
    virtual void handle_DataReceived(const int& req_id, const size_t& length) override;
    virtual void handle_RequestComplete(const int& req_id, const bool success) override;

protected:

    virtual ~Webengine() = default;

    //////

    void _init_angelscript_engine();


    /////

    IOServiceIPCClient* ioservice_ipcclient_;

    /* map from the request id that we generate to the resource for
     * which we're requesting. this is like chrome's
     * resource_disptacher's "pending_requests_"
     */
    std::map<int, Resource*> pending_requests_;
};

}

#endif /* webengine_hpp */
