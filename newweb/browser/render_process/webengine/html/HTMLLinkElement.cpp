
#include "HTMLLinkElement.hpp"
#include "../dom/Document.hpp"

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

    if (is_blocking_stylesheet_) {
        const auto resource = document->fetcher()->getResource(resInstNum_);
        if (resource && !resource->isFinished()) {
            document->addPendingSheet(this);
        }
    }
}

void
HTMLLinkElement::notifyFinished(Resource*, bool success)
{
    if (is_blocking_stylesheet_) {
        document()->removePendingSheet(this);
    }
}

} //namespace
