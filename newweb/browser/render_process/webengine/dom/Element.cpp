
#include "../../../../utility/easylogging++.h"

#include "Element.hpp"

#include "Document.hpp"



#define _LOG_PREFIX(inst) << "elem:" << instNum() \
    << " (objId= " << objId() << "): "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


namespace blink {


Element::Element(
    const uint32_t& instNum,
    const std::string tag,
    Document* document)
    : Node(instNum)
    , resInstNum_(0)
    , tag_(tag)
{}

void
Element::setResInstNum(const uint32_t& resInstNum)
{
    vlogself(2) << "begin, resInstNum= " << resInstNum;

    auto fetcher = document()->fetcher();

    auto resource = fetcher->getResource(resInstNum);
    CHECK_NOTNULL(resource.get());

    if (resource->isFinished()) {
        vlogself(2) << "resource has finished, but did it succeed?";
        if (!resource->errorOccurred()) {
            // great! fire our "load" event if any
            CHECK(0) << "todo";
        } else {
            logself(WARNING) << "resource failed to load";
        }
    } else {
        if (!resource->isLoading()) {
            vlogself(2) << "tell resource to load";
            resource->load();
        }

        resource->addClient(this);
    }

    vlogself(2) << "done";
}

}
