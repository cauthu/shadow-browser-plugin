#ifndef HTMLDocumentParser_hpp
#define HTMLDocumentParser_hpp


#include <vector>
#include <utility> // for pair
#include <string>

#include "../../../../../utility/object.hpp"
#include "../../page_model.hpp"
#include "../../fetch/ResourceFetcher.hpp"


namespace blink {

class Document;
    class HTMLScriptElement;
    class Webengine;

class HTMLDocumentParser : public Object
{
public:
    typedef std::unique_ptr<HTMLDocumentParser, Destructor> UniquePtr;


    explicit HTMLDocumentParser(const PageModel*,
                                Document*,
                                Webengine*,
                                ResourceFetcher*);

    void pump_parser();

    /* more html bytes are received from network */
    void appendBytes(size_t len);

    /* there will be no more html data after this */
    void finish_receive();

    bool hasParserBlockingScript() const;

protected:

    virtual ~HTMLDocumentParser() = default;

    void _do_preload_scanning();
    int _find_element_idx_to_begin_preload_scanning();

    void _add_element_to_doc(const uint32_t& elemInstNum);

    ///////////

    const PageModel* page_model_;
    Document* document_;
    Webengine* webengine_;
    ResourceFetcher* resource_fetcher_;

    HTMLScriptElement* parser_blocking_script_;
    
    /* num of bytes of the html that we received */
    size_t num_bytes_received_;
    /* whether we have fully received the html data, i.e., there will
     * be no more data */
    bool done_received_;

    /* num of bytes of the html that we have PARSED */
    size_t num_bytes_parsed_;

    size_t num_preload_scanned_bytes_;
    bool no_more_preload_scanning_necessary_;

    /* a list of <element-byte-offset, element instNum> pairs. we
     * assume that the list is sorted in ascending element byte
     * offsets.
     *
     * NOTE!!! these are byte offsets, i.e., 0-based
     */
    std::vector<std::pair<size_t, uint32_t> > element_locations_;

    /* the idx of the element that we will parse next---once enough
     * bytes are available */
    size_t element_loc_idx_;

};

} // end namespace blink

#endif /* HTMLDocumentParser_hpp */
