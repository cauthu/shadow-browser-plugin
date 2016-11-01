#ifndef HTMLLinkElement_hpp
#define HTMLLinkElement_hpp


#include "../dom/Element.hpp"
#include "../page_model.hpp"

namespace blink {

    class Document;
    class Resource;

class HTMLLinkElement : public Element
{
public:

    typedef std::unique_ptr<HTMLLinkElement, Destructor> UniquePtr;

    explicit HTMLLinkElement(const uint32_t& instNum,
                             Document*,
                             const PageModel::ElementInfo& info);

    const bool& is_blocking_stylesheet() const { return is_blocking_stylesheet_; }
    const std::string& rel() const { return rel_; }

protected:

    virtual ~HTMLLinkElement() = default;

    virtual void notifyFinished(Resource*, bool success) override;
    virtual void onResInstNumChanged() override;

    const std::string rel_;
    const bool is_blocking_stylesheet_;

    // whether we are currently blocking. this is so that, if we have
    // added ourselves as pending style sheet, then before we remove
    // ourselves, we are set to a different resource, so we shouldn't
    // incremented again
    bool currently_blocking_ = false;
};

} //namespace

#endif
