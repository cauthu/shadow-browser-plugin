#ifndef CSSStyleSheetResource_hpp
#define CSSStyleSheetResource_hpp


#include "Resource.hpp"

namespace blink {

class CSSStyleSheetResource : public Resource
{
public:
    typedef std::unique_ptr<CSSStyleSheetResource, Destructor> UniquePtr;


    CSSStyleSheetResource(const uint32_t& instNum,
                          const uint32_t& parse_duration_ms)
        : Resource(instNum)
        , parse_duration_ms_(parse_duration_ms)
    {}

    const uint32_t& parse_duration_ms() const { return parse_duration_ms_; }

protected:

    virtual ~CSSStyleSheetResource() = default;

    const uint32_t parse_duration_ms_;
};

}

#endif /* CSSStyleSheetResource_hpp */
