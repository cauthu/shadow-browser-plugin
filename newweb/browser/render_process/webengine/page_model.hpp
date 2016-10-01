#ifndef page_model_hpp
#define page_model_hpp

#include <memory>
#include <rapidjson/document.h>
#include <string>
#include <vector>

#include "../../../utility/object.hpp"


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
        uint16_t priority;
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
        uint32_t initial_resInstNum;
        std::vector<std::pair<std::string, uint32_t> > event_handling_scopes;

        // for script elements
        bool exec_async;
        bool exec_immediately;
        bool is_parser_blocking;
        uint32_t run_scope_id;

        // for link elements
        std::string rel;
        bool is_blocking_css;
    };

    explicit PageModel(const char* json_fpath);

    bool get_main_resource_info(ResourceInfo&) const;

    /* popuplate "res_info" with information about the resource
     */
    bool get_resource_info(const uint32_t& resInstNum,
                           ResourceInfo& res_info) const;

    /* popuplate "elem_info" with information about the element
     */
    bool get_element_info(const uint32_t& elemInstNum,
                           ElementInfo& elem_info) const;

    /* each pair is element's closing byte OFFSET-within-HTML, to
     * element's instNum */
    void get_main_html_element_byte_offsets(std::vector<std::pair<size_t, uint32_t> >&) const;

    uint32_t get_element_initial_resInstNum(const uint32_t& elemInstNum) const;

    void get_execution_scope_statements(uint32_t scope_id,
                                        std::vector<std::string>& statements) const;

private:

    virtual ~PageModel() = default;


    // bool _get_element_json_obj(const uint32_t elemInstNum,
    //                            const rapidjson::Value& element) const;

    ///////////////


    rapidjson::Document model_json;

    ////

};

}

#endif /* page_model_hpp */
