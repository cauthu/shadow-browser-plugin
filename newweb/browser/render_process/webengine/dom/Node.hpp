
#ifndef Node_hpp
#define Node_hpp

#include "../events/EventTarget.hpp"

namespace blink {

class Node : public EventTarget
{
public:

    typedef std::unique_ptr<Node, Destructor> UniquePtr;



protected:

    virtual ~Node() = default;


};


} // end namespace blink

#endif // Node_hpp
