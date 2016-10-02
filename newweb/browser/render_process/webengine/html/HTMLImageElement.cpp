
#include "HTMLImageElement.hpp"


namespace blink {

HTMLImageElement::HTMLImageElement(
    const uint32_t& instNum,
    Document* document,
    const PageModel::ElementInfo& info)
    : Element(instNum, "img", document, info)
{
}

}
