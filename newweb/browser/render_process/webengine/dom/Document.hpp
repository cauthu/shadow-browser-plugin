#ifndef Document_hpp
#define Document_hpp

#include <memory>
#include <event2/event.h>

#include "../../../../utility/object.hpp"
#include "../../../../utility/timer.hpp"

#include "../page_model.hpp"
#include "../fetch/ResourceFetcher.hpp"
#include "../fetch/Resource.hpp"
#include "../html/parser/HTMLDocumentParser.hpp"

#include "../events/EventTarget.hpp"

namespace blink {

class Webengine;
class Element;

class Document : public EventTarget
               , public ResourceClient
{
public:
    typedef std::unique_ptr<Document, Destructor> UniquePtr;

    explicit Document(struct ::event_base*,
                      const uint32_t& instNum,
                      Webengine*,
                      const PageModel*,
                      ResourceFetcher*);

    /* start loading the page */
    void load();

    /* element being added by parser or script. if the element is a
     * parser blocking script, then we will call
     * set_parser_blocking_script() on the parser.
     */
    void add_elem(const uint32_t& elemInstNum);
    void set_elem_res(const uint32_t elemInstNum, const uint32_t resInstNum);

    ResourceFetcher* fetcher() { return resource_fetcher_; }

    bool isScriptExecutionReady() const;

    void addPendingSheet(Element* element);
    void removePendingSheet(Element* element);

    void finishedParsing();

    Webengine* webengine() { return webengine_; }

protected:

    virtual ~Document() = default;

    /* ResourceClient interface, to know about main resource */
    virtual void notifyFinished(Resource*, bool success) override;
    virtual void responseReceived(Resource*) override;
    virtual void dataReceived(Resource*, size_t length) override;

    void _load_main_resource();
    void _didLoadAllScriptBlockingResources();
    void _executeScriptsWaitingForResourcesTimerFired(Timer*);

    struct ::event_base* evbase_;
    Webengine* webengine_;
    const PageModel* page_model_;
    ResourceFetcher* resource_fetcher_;

    enum class DocumentState
    {
        INITIAL, LOADING
            } state_;

    std::shared_ptr<Resource> main_resource_;
    HTMLDocumentParser::UniquePtr parser_;

    /* number of blocking stylesheets that are still pending, i.e.,
     * not loaded/parsed yet. this typically blocks js execution.
     */
    int pendingStylesheets_;

    Timer::UniquePtr executeScriptsWaitingForResourcesTimer_;

    /* map key is elemInstNum */
    std::map<uint32_t, std::shared_ptr<Element> > elements_;

    // render update scopes don't run until we have body element. we
    // have seen scripts on cnn.com add and remove body elements, but
    // for now we don't support removing body element
    bool has_body_element_;

    bool finished_parsing_;
};

} //namespace

#endif /* Document_hpp */
