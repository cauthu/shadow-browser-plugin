
#include "../../../../utility/easylogging++.h"

#include "HTMLScriptElement.hpp"


#define _LOG_PREFIX(inst) << "script elem:" << instNum() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)

namespace blink {


HTMLScriptElement::HTMLScriptElement(
    const uint32_t instNum,
    Document* document,
    const PageModel::ElementInfo& info)
    : Element(instNum, "script", document, info)
    , blocks_parser_(info.is_parser_blocking)
    , exec_immediately_(info.exec_immediately)
    , exec_async_(info.exec_async)
    , run_scope_id_(info.run_scope_id)
{
    // cannot be both true
    CHECK( ! (exec_immediately_ && exec_async_) );

    CHECK_GT(run_scope_id_, 0);
}

void
HTMLScriptElement::setResInstNum(const uint32_t& resInstNum)
{
    vlogself(2) << "begin, res:" << resInstNum;

    if ((this->resInstNum() > 0) && (this->resInstNum() != resInstNum)) {
        logself(FATAL) << "we don't support changing a script's resource "
                       << "after it has already been set";
    }

    Element::setResInstNum(resInstNum);

    vlogself(2) << "done";
}

}
