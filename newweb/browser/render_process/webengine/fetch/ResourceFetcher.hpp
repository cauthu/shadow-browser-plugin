#ifndef ResourceFetcher_hpp
#define ResourceFetcher_hpp


#include <memory>
#include <map>


#include "../../../../utility/object.hpp"
#include "../page_model.hpp"

#include "Resource.hpp"


namespace blink {

class ResourceFetcher final : public Object
{
public:
    typedef std::unique_ptr<ResourceFetcher, Destructor> UniquePtr;

    explicit ResourceFetcher(Webengine*, const PageModel*);

    std::shared_ptr<Resource> getResource(const uint32_t& resInstNum);
    std::shared_ptr<Resource> getMainResource();

    void preload(std::vector<uint32_t>& resInstNums);

    void incrementRequestCount(const Resource*);
    void decrementRequestCount(const Resource*);

private:

    virtual ~ResourceFetcher();

    Webengine* webengine_;
    const PageModel* page_model_;

    /* if this is > 0, then the document is not considered "loaded" */
    uint32_t requestCount_;

    std::map<uint32_t, std::shared_ptr<Resource> > documentResources_;

};

} // end namespace blink

#endif /* end ResourceFetcher_hpp */
