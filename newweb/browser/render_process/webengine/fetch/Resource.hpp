#ifndef Resource_hpp
#define Resource_hpp

#include <set>

#include "../../../../utility/object.hpp"
#include "../page_model.hpp"

namespace blink {

class Resource;
class Webengine;

class ResourceClient
{
public:
    virtual void notifyFinished(Resource*, bool success) = 0;

    /* these are from RawResourceClient (a derived class of
     * ResourceClient) but i'll use them here for convenience
     */
    virtual void responseReceived(Resource* /*, const ResourceResponse*/) {}
    virtual void dataReceived(Resource*, size_t length) {}
    virtual void redirectReceived(Resource* /*, ResourceRequest&, const ResourceResponse&*/) { }

};

class Resource : public Object
{
public:
    typedef std::unique_ptr<Resource, Destructor> UniquePtr;


    Resource(const PageModel::ResourceInfo&,
             Webengine*);

    /* start loading the resource */
    void load();

    /* tell the resource more data has been received */
    void appendData(size_t length);

    /* tell the resource it has now finished receiving the response */
    void finish(bool success);

    void addClient(ResourceClient*);

    bool isLoading() const { return load_state_ == LoadState::LOADING; }
    bool isFinished() const { return load_state_ == LoadState::FINISHED; }

    bool errorOccurred() const { return errored_; }

    /* whether this resource should be finished before the document
     * (technically the frame loader --- see FrameLoader.cpp in webkit
     * -- which checks the fetcher's "requestCount" when determing
     * whether the frameloader is completed) is considered loaded */
    bool part_of_page_loaded_check() const { return res_info_.part_of_page_loaded_check; }

    const uint32_t& instNum() const;
        
protected:

    virtual ~Resource() = default;


    /////////

    void _notify_new_data(const size_t&);
    void _notify_finished(bool);

    void _load_next_chain_entry();

    const PageModel::ResourceInfo res_info_;

    /* "finished" means it's not actively loading, but the load could
     * be successful or failure
     */
    enum class LoadState {
        INITIAL, LOADING, FINISHED
            } load_state_;

    /* total number of BODY bytes we have received, across all the
     * requests in the chain
     */
    size_t cumulative_resp_body_bytes_;

    /* whether an error occurred */
    bool errored_;

    std::set<ResourceClient*> m_clients;


    // TODO: handle redirects: (1) one way is to actually instruct the
    // server to respond with "redirect" status code, and the io
    // service must be updated to understand and handle such
    // responses. (2) alternative is to just issue "normal" requests
    // to io service, and when it tells us about "datareceived" and
    // "finish" we handle specially---i.e., don't notify clients---if
    // we're not yet at the end of redirect chain, and keep doing that
    // until we are at the end of the redirect chain, then we can
    // notify clients when data is received and when it's finished

    // init to -1
    int current_req_chain_idx_;

    /* number of body bytes received for ONLY the current request */
    size_t current_req_body_bytes_recv_;

    Webengine* webengine_;
};

}

#endif
