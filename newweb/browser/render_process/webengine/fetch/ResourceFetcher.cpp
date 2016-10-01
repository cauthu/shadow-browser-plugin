
#include "../../../../utility/easylogging++.h"
#include "../../../../utility/common.hpp"

#include "../webengine.hpp"
#include "ResourceFetcher.hpp"
#include "CSSStyleSheetResource.hpp"



#define _LOG_PREFIX(inst) << ""

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


using std::shared_ptr;

namespace blink {

ResourceFetcher::ResourceFetcher(Webengine* webegine,
                                 const PageModel* page_model)
    : webengine_(webegine)
    , page_model_(page_model)
    , requestCount_(0)
{
    

}

shared_ptr<Resource>
ResourceFetcher::getResource(const uint32_t& instNum)
{
    if (inMap(documentResources_, instNum)) {
        return documentResources_[instNum];
    }

    vlogself(2) << "resource not yet exist";

    // if instnum not in the map yet, the map will create new
    // entry with default value and return it, which will be null
    shared_ptr<Resource> resource;

    PageModel::ResourceInfo res_info;
    auto rv = page_model_->get_resource_info(instNum, res_info);
    CHECK(rv);

    if (res_info.type != "css") {
        resource.reset(
            new Resource(res_info, webengine_, this),
            [](Resource* res) { res->destroy(); }
            );
    } else {
        resource.reset(
            new CSSStyleSheetResource(
                res_info, webengine_, this, res_info.css_parse_dur_ms),
            [](CSSStyleSheetResource* res) { res->destroy(); }
            );
    }

    const auto ret = documentResources_.insert(
        make_pair(resource->instNum(), resource));
    CHECK(ret.second);

    return resource;
}

shared_ptr<Resource>
ResourceFetcher::getMainResource()
{
    PageModel::ResourceInfo res_info;
    auto rv = page_model_->get_main_resource_info(res_info);
    CHECK(rv);

    shared_ptr<Resource> resource(
        new Resource(res_info, webengine_, this),
        [](Resource* res) { res->destroy(); }
        );

    const auto ret = documentResources_.insert(
        make_pair(resource->instNum(), resource));
    CHECK(ret.second);

    return resource;
}

void
ResourceFetcher::preload(std::vector<uint32_t>& resInstNums)
{
    vlogself(2) << "begin, num to preload= " << resInstNums.size();

    for (auto resInstNum : resInstNums) {
        CHECK_GT(resInstNum, 0);

        vlogself(2) << "preloading res:" << resInstNum;

        shared_ptr<Resource> resource = getResource(resInstNum);
        CHECK_NOTNULL(resource.get());

        if (!resource->isLoading() && !resource->isFinished()) {
            // we start the load only if it is not loading and has not
            // finished; for example, multiple elements can reference
            // the same resource, and the earlier element has already
            // started the load of the resource
            resource->load();
        }

        // getResource() should have already added to
        // documentResources_
        CHECK(inMap(documentResources_, resInstNum));
        
    }
    
    vlogself(2) << "done";
}

void
ResourceFetcher::incrementRequestCount(const Resource* resource)
{
    CHECK(resource->part_of_page_loaded_check());
    ++requestCount_;
    vlogself(2) << "res:" << resource->instNum()
                << " increments requestCount_ to " << requestCount_;
}

void
ResourceFetcher::decrementRequestCount(const Resource* resource)
{
    CHECK(resource->part_of_page_loaded_check());
    --requestCount_;
    vlogself(2) << "res:" << resource->instNum()
                << " decrements requestCount_ to " << requestCount_;
    CHECK_GE(requestCount_, 0);
}

} // end namespace blink
