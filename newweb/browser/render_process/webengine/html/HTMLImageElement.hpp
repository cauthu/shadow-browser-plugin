#ifndef HTMLImageElement_hpp
#define HTMLImageElement_hpp


#include "../dom/Element.hpp"


namespace blink {

    class Document;

class HTMLImageElement : public Element
{
public:

    typedef std::unique_ptr<HTMLImageElement, Destructor> UniquePtr;

    explicit HTMLImageElement(const uint32_t& instNum,
                              Document*);

protected:

    virtual ~HTMLImageElement() = default;

};

} //namespace

#endif
