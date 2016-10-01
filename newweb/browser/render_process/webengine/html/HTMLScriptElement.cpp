
#include "HTMLScriptElement.hpp"


namespace blink {


HTMLScriptElement::HTMLScriptElement(
    const uint32_t instNum,
    Document* document,
    bool blocks_parser,
    bool exec_immediately,
    bool exec_async,
    const uint32_t run_scope_id)
    : Element(instNum, "script", document)
    , blocks_parser_(blocks_parser)
    , exec_immediately_(exec_immediately)
    , exec_async_(exec_async)
    , run_scope_id_(run_scope_id)
{
    // cannot be both true
    CHECK( ! (exec_immediately_ && exec_async_) );

    CHECK_GT(run_scope_id_, 0);
}

}
