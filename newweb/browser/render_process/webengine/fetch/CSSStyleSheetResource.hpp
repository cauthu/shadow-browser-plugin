#ifndef CSSStyleSheetResource_hpp
#define CSSStyleSheetResource_hpp


#include "Resource.hpp"

namespace blink {

class CSSStyleSheetResource : public Resource
{
public:
    typedef std::unique_ptr<CSSStyleSheetResource, Destructor> UniquePtr;


    CSSStyleSheetResource(const PageModel::ResourceInfo& res_info,
                          Webengine* webengine, ResourceFetcher*,
                          const int32_t& parse_dur_ms,
                          const uint32_t& parse_scope_id);

    // const uint32_t& parse_duration_ms() const { return parse_duration_ms_; }

protected:

    virtual ~CSSStyleSheetResource() = default;

    virtual void _really_did_succeed();

    /////////////

    const int32_t parse_dur_ms_;
    const uint32_t parse_scope_id_;
};

}

#endif /* CSSStyleSheetResource_hpp */
