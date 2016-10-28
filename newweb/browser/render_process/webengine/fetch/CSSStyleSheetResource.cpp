
#include "../../../../utility/easylogging++.h"

#include "CSSStyleSheetResource.hpp"
#include "../webengine.hpp"

namespace blink {

CSSStyleSheetResource::CSSStyleSheetResource(
    const PageModel::ResourceInfo& res_info,
    Webengine* webengine, ResourceFetcher* fetcher,
    const int32_t& parse_dur_ms,
    const uint32_t& parse_scope_id)
    : Resource(res_info, webengine, fetcher)
    , parse_dur_ms_(parse_dur_ms)
    , parse_scope_id_(parse_scope_id)
{
    CHECK((parse_dur_ms_ >= 0) || (parse_scope_id_ > 0))
        << "bad css stylesheet res:" << instNum();
}

void
CSSStyleSheetResource::_really_did_succeed()
{
    if (parse_dur_ms_ >= 0) {
        VLOG(2) << "parse for " << parse_dur_ms_ << " ms";
        webengine_->msleep(parse_dur_ms_);
    } else {
        VLOG(2) << "execute parse scope " << parse_scope_id_;
        CHECK(parse_scope_id_ > 0);
        webengine_->execute_scope(parse_scope_id_);
    }

    DestructorGuard dg(this);
    Resource::_really_did_succeed();
}

} // end namespace blink
