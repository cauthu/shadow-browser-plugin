#ifndef object_hpp
#define object_hpp


#include "DelayedDestruction.h"

/* a generic Object that will assign a uniq objId for every instance
 * (just a monotonically increasing instance number) and supports
 * delayed destruction
 */

class Object : public folly::DelayedDestruction
{
public:
    typedef std::unique_ptr<Object, Destructor> UniquePtr;

    /* every Object every created will have unique id */
    const uint32_t& objId() const;

    /* for DelayedDestruction */
    virtual void destroy();

protected:

    Object();

    // destructor should be private or protected to prevent direct
    // deletion. we're using folly::DelayedDestruction
    virtual ~Object() = default;

    const uint32_t instNum_;

};

#endif /* end object_hpp */
