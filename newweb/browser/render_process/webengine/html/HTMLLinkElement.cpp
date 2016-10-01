
#include "HTMLLinkElement.hpp"
#include "../dom/Document.hpp"

namespace blink {

HTMLLinkElement::HTMLLinkElement(
    const uint32_t& instNum,
    Document* document,
    const std::string rel,
    bool is_blocking_stylesheet)
    : Element(instNum, "link", document)
    , rel_(rel)
    , is_blocking_stylesheet_(is_blocking_stylesheet)
{
    CHECK_NOTNULL(document);

    // right now we support only css stylesheet rel
    CHECK_EQ(rel, "stylesheet") << "rel \"" << rel << "\" not yet supported";

    if (is_blocking_stylesheet_) {
        document->addPendingSheet(this);
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
