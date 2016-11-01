
#include "HTMLLinkElement.hpp"
#include "../dom/Document.hpp"

#define _LOG_PREFIX(inst) << "link elem:" << (inst)->instNum() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)

namespace blink {

HTMLLinkElement::HTMLLinkElement(
    const uint32_t& instNum,
    Document* document,
    const PageModel::ElementInfo& info)
    : Element(instNum, "link", document, info)
    , rel_(info.rel)
    , is_blocking_stylesheet_(info.is_blocking_css)
{
    CHECK_NOTNULL(document);

    // right now we support only css stylesheet rel
    CHECK_EQ(rel_, "stylesheet") << "rel \"" << rel_ << "\" not yet supported";

    if (is_blocking_stylesheet_ && (resInstNum_ > 0)) {
        const auto resource = document->fetcher()->getResource(resInstNum_);
        if (resource && !resource->isFinished() && !currently_blocking_) {
            vlogself(2) << "resource is loading, so we are a blocking sheet";
            currently_blocking_ = true;
            document->addPendingSheet(this);
        }
    }
}

void
HTMLLinkElement::onResInstNumChanged()
{
    vlogself(2) << "begin, res:" << resInstNum_;

    auto fetcher = document()->fetcher();
    CHECK_NOTNULL(fetcher);

    auto resource = fetcher->getResource(resInstNum_);
    CHECK_NOTNULL(resource.get());

    if (resource->isLoading()) {
        if (is_blocking_stylesheet_ && !currently_blocking_) {
            vlogself(2) << "resource is loading, so we are a blocking sheet";
            currently_blocking_ = true;
            document()->addPendingSheet(this);
        }
    }

    vlogself(2) << "done";
}

void
HTMLLinkElement::notifyFinished(Resource*, bool success)
{
    if (is_blocking_stylesheet_ && currently_blocking_) {
        currently_blocking_ = false;
        document()->removePendingSheet(this);
    }
}

} //namespace
