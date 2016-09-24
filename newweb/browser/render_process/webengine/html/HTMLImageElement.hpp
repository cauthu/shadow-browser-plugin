#ifndef HTMLImageElement_hpp
#define HTMLImageElement_hpp


#include "../dom/Element.hpp"

namespace blink {

class HTMLImageElement : /* public HTMLElement, */ public Element
{
public:

    typedef std::unique_ptr<HTMLImageElement, Destructor> UniquePtr;


protected:

    virtual ~HTMLImageElement() = default;

};

} //namespace

#endif
