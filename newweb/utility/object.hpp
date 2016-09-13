#ifndef object_hpp
#define object_hpp


#include "folly/DelayedDestruction.h"


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


    // make this final to prevent child classes from overriding, which
    // might render DelayedDestruction useless, i.e., the child
    // doesn't call DelayedDestruction::destroy()
    virtual void destroy() override final { DelayedDestruction::destroy(); }

    void setData(void* data) { data_ = data; }
    void* data() const { return data_; }

protected:

    Object();

    // destructor should be private or protected to prevent direct
    // deletion. we're using folly::DelayedDestruction
    virtual ~Object() = default;

    const uint32_t instNum_;

    /* opaque pointer for user to set whatever he wants */
    void* data_;
};

#endif /* end object_hpp */
