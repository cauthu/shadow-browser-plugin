#ifndef Document_hpp
#define Document_hpp

#include <memory>

#include "../../../../utility/object.hpp"

// #include "../webengine.hpp"
#include "../page_model.hpp"
#include "../fetch/ResourceFetcher.hpp"
#include "../fetch/Resource.hpp"
#include "../html/parser/HTMLDocumentParser.hpp"

#include "Node.hpp"
#include "Element.hpp"

namespace blink {

class Webengine;

class Document : public Node
               , public ResourceClient
{
public:
    typedef std::unique_ptr<Document, Destructor> UniquePtr;

    explicit Document(const uint32_t& instNum,
                      Webengine*,
                      const PageModel*,
                      ResourceFetcher*);

    /* start loading the page */
    void load();

    /* element being added by parser or script */
    void addElement(const uint32_t& elemInstNum);

    ResourceFetcher* fetcher() { return resource_fetcher_; }
    
protected:

    virtual ~Document() = default;

    /* ResourceClient interface, to know about main resource */
    virtual void notifyFinished(Resource*, bool success) override;
    virtual void responseReceived(Resource*) override;
    virtual void dataReceived(Resource*, size_t length) override;

    void _load_main_resource();

    Webengine* webengine_;
    const PageModel* page_model_;
    ResourceFetcher* resource_fetcher_;

    enum class DocumentState
    {
        INITIAL, LOADING
            } state_;

    std::shared_ptr<Resource> main_resource_;
    HTMLDocumentParser::UniquePtr parser_;

};

} //namespace

#endif /* Document_hpp */
