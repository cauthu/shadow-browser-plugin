
#include "../../../../utility/easylogging++.h"

#include "CSSStyleSheetResource.hpp"
#include "../webengine.hpp"

namespace blink {

CSSStyleSheetResource::CSSStyleSheetResource(
    const PageModel::ResourceInfo& res_info,
    Webengine* webengine, ResourceFetcher* fetcher,
    const uint32_t& parse_dur_ms)
    : Resource(res_info, webengine, fetcher)
    , parse_dur_ms_(parse_dur_ms)
{
}

void
CSSStyleSheetResource::_really_did_succeed()
{
    VLOG(2) << "parse for " << parse_dur_ms_ << " ms";
    webengine_->msleep(parse_dur_ms_);

    DestructorGuard dg(this);
    Resource::_really_did_succeed();
}

} // end namespace blink
