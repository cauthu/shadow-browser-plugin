#ifndef XMLHttpRequest_hpp
#define XMLHttpRequest_hpp

#include <memory>

#include "../../../../utility/object.hpp"
#include "../../../../utility/timer.hpp"
#include "../page_model.hpp"

#include "../events/EventTarget.hpp"
#include "../fetch/Resource.hpp"


namespace blink {

    class ResourceFetcher;
    class Webengine;

class XMLHttpRequest final : public EventTarget
                           , public ResourceClient
{
public:
    typedef std::unique_ptr<XMLHttpRequest, Destructor> UniquePtr;

    explicit XMLHttpRequest(Webengine*,
                            ResourceFetcher*,
                            const PageModel::XMLHttpRequestInfo&);

    void send();

    const uint32_t& instNum() const { return info_.instNum; }

protected:

    virtual ~XMLHttpRequest() = default;

    void _load_next_chain_entry();
    bool _receiving_final_resource() const;
    void _really_did_succeed();

    /* implement ResourceClient interface */
    virtual void notifyFinished(Resource*, bool success) override;
    virtual void responseReceived(Resource*) override {}
    virtual void dataReceived(Resource*, size_t length) override {}

    //////////

    Webengine* webengine_;
    ResourceFetcher* resource_fetcher_;

    const PageModel::XMLHttpRequestInfo info_;

    enum class XhrState {
        INITIAL, LOADING, FINISHED
    } load_state_;

    // init to -1
    int current_res_chain_idx_;
    std::shared_ptr<Resource> current_loading_resource_;

};

} // namespace blink

#endif // XMLHttpRequest_hpp
