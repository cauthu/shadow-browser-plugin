#ifndef HTMLLinkElement_hpp
#define HTMLLinkElement_hpp


#include "../dom/Element.hpp"

namespace blink {

class HTMLLinkElement : /* public HTMLElement, */ public Element
{
public:

    typedef std::unique_ptr<HTMLLinkElement, Destructor> UniquePtr;

    HTMLLinkElement(const uint32_t& instNum)
        : Element(instNum)
        , is_block_stylesheet(false)
    {}

    bool is_block_stylesheet;


protected:

    virtual ~HTMLLinkElement() = default;

};

} //namespace

#endif
