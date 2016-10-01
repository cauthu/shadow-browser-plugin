
#include "HTMLImageElement.hpp"


namespace blink {

HTMLImageElement::HTMLImageElement(
    const uint32_t& instNum,
    Document* document)
    : Element(instNum, "img", document)
{
}

}
