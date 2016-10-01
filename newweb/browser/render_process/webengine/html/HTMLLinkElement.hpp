#ifndef HTMLLinkElement_hpp
#define HTMLLinkElement_hpp


#include "../dom/Element.hpp"

namespace blink {

    class Document;
    class Resource;

class HTMLLinkElement : public Element
{
public:

    typedef std::unique_ptr<HTMLLinkElement, Destructor> UniquePtr;

    explicit HTMLLinkElement(const uint32_t& instNum,
                             Document*,
                             const std::string rel,
                             bool is_blocking_stylesheet);

    const bool& is_blocking_stylesheet() const { return is_blocking_stylesheet_; }
    const std::string& rel() const { return rel_; }

protected:

    virtual ~HTMLLinkElement() = default;

    virtual void notifyFinished(Resource*, bool success) override;

    const std::string rel_;
    const bool is_blocking_stylesheet_;

};

} //namespace

#endif
