
#ifndef Element_hpp
#define Element_hpp

#include "../events/EventTarget.hpp"
#include "../fetch/Resource.hpp"
#include "../page_model.hpp"

#include <string>

namespace blink {

    class Document;

class Element : public EventTarget
              , public ResourceClient
{
public:
    typedef std::unique_ptr<Element, Destructor> UniquePtr;

    explicit Element(const uint32_t& instNum,
                     const std::string tag,
                     Document*,
                     const PageModel::ElementInfo&);

    const uint32_t& resInstNum() const { return resInstNum_; }
    virtual void setResInstNum(const uint32_t& resInstNum);

protected:
    
    virtual ~Element() = default;

    /* implement ResourceClient interface */
    virtual void notifyFinished(Resource*, bool success) {}
    virtual void responseReceived(Resource*) {}
    virtual void dataReceived(Resource*, size_t length) {}

    Document* document() { return document_; }

    virtual void onResInstNumChanged() {}

    ///////

    Document* document_;

    /* the resource this element refers to, e.g., via the "src"
     * attribute of an img or script.
     *
     * this can be changed by javascript, which can change an
     * element's src attribute for example
     *
     * if 0, then it's currently not referencing any resource
     */
    uint32_t resInstNum_;

    const std::string tag_;
};

} // end namespace blink

#endif // Element_hpp
