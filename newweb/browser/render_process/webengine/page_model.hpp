#ifndef page_model_hpp
#define page_model_hpp

#include <memory>
#include <rapidjson/document.h>
#include <string>
#include <vector>

#include "../../../utility/object.hpp"

#include "fetch/ResourceLoadPriority.hpp"

namespace blink {

class PageModel : public Object
{
public:
    typedef std::unique_ptr<PageModel, Destructor> UniquePtr;

    struct RequestInfo
    {
        std::string host;
        std::string method;
        uint16_t port;
        ResourceLoadPriority priority;
        size_t req_total_size;
        size_t resp_meta_size;
        size_t resp_body_size;
    };

    struct ResourceInfo
    {
        uint32_t instNum;
        bool part_of_page_loaded_check;
        std::vector<RequestInfo> req_chain;
        std::string type;

        // valid only if "type" is "css"
        uint32_t css_parse_dur_ms;
    };

    struct ElementInfo
    {
        uint32_t instNum;
        std::string tag;
        std::vector<std::pair<std::string, uint32_t> > event_handling_scopes;
        uint32_t initial_resInstNum;

        // for script elements
        bool exec_async;
        bool exec_immediately;
        bool is_parser_blocking;
        uint32_t run_scope_id;

        // for link elements
        std::string rel;
        bool is_blocking_css;

        // we don't have any specific to <img> and <body> elements
    };

    struct DOMTimerInfo
    {
        uint32_t timerID;
        uint32_t interval_ms;
        bool singleShot;
        std::vector<uint32_t> fired_scope_ids;
    };

    struct XMLHttpRequestInfo
    {
        uint32_t instNum;
        std::vector<uint32_t> res_chain;
        std::vector<std::pair<std::string, uint32_t> > event_handling_scopes;
    };

    struct DocumentInfo
    {
        // for the main html
        std::vector<std::pair<size_t, uint32_t> > html_element_byte_offsets;
        std::vector<std::pair<std::string, uint32_t> > event_handling_scopes;
    };

    explicit PageModel(const char* json_fpath);

    bool get_main_resource_info(ResourceInfo&) const;

    /* popuplate "res_info" with information about the resource
     */
    bool get_resource_info(const uint32_t& resInstNum,
                           ResourceInfo& res_info) const;

    void get_all_resource_instNums(std::vector<uint32_t>&) const;

    /* popuplate "elem_info" with information about the element
     */
    bool get_element_info(const uint32_t& elemInstNum,
                           ElementInfo& elem_info) const;

    /* each pair is element's closing byte OFFSET-within-HTML, to
     * element's instNum */
    bool get_main_html_element_byte_offsets(std::vector<std::pair<size_t, uint32_t> >&) const;

    bool get_main_doc_info(DocumentInfo&) const;

    uint32_t get_element_initial_resInstNum(const uint32_t& elemInstNum) const;

    uint32_t get_initial_render_tree_update_scope_id() const;

    /* return true if the scope is found, false otherwise */
    bool get_execution_scope_statements(uint32_t scope_id,
                                        std::vector<std::string>& statements) const;

    bool get_dom_timer_info(uint32_t timerID,
                            DOMTimerInfo& timer_info) const;

    bool get_xhr_info(uint32_t xhrInstNum,
                      XMLHttpRequestInfo& xhr_info) const;

private:

    virtual ~PageModel() = default;

    bool _get_event_handling_scopes(
        const rapidjson::Value& object,
        std::vector<std::pair<std::string, uint32_t> >&) const;

    bool _get_uint_array(const rapidjson::Value& array,
                         std::vector<uint32_t>& uints) const;

    ///////////////


    rapidjson::Document model_json;

    ////

};

}

#endif /* page_model_hpp */
