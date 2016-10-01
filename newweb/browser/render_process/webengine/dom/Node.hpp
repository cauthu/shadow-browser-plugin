
#ifndef Node_hpp
#define Node_hpp

#include "../events/EventTarget.hpp"

namespace blink {

    class Document;

class Node : public EventTarget
{
public:

    typedef std::unique_ptr<Node, Destructor> UniquePtr;


protected:

    Document* document() { return document_; }

    explicit Node(const uint32_t& instNum,
                  Document*);

    virtual ~Node() = default;

private:

    Document* document_;

};


} // end namespace blink

#endif // Node_hpp
