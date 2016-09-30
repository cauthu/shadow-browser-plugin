#ifndef page_model_hpp
#define page_model_hpp

#include <memory>
#include <rapidjson/document.h>
#include <string>
#include <vector>

#include "../../../utility/object.hpp"


namespace blink {

class Webengine;

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
    };
      

    explicit PageModel(const char* json_fpath,
                       Webengine* webengine);

    bool get_main_resource_info(ResourceInfo&);

    /* popuplate "res_info" with information about the resource
     */
    bool get_resource_info(const uint32_t& resInstNum,
                           ResourceInfo& res_info) const;

private:

    virtual ~PageModel() = default;


    ///////////////


    Webengine* webengine_;

    rapidjson::Document model_json;

    ////

};

}

#endif /* page_model_hpp */
