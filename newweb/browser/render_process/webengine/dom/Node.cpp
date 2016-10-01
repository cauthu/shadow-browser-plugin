
#include "../../../../utility/easylogging++.h"

#include "Node.hpp"


namespace blink {

Node::Node(
    const uint32_t& instNum,
    Document* document)
    : EventTarget(instNum)
    , document_(document)
{
    CHECK_NOTNULL(document_);
}



}
