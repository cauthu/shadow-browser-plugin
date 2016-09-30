
#include <fstream>
#include <string>
#include <sstream>      // std::stringstream
#include <boost/lexical_cast.hpp>

#include "page_model.hpp"

#include "../../../utility/easylogging++.h"

#include "webengine.hpp"

using std::string;
using boost::lexical_cast;

namespace json = rapidjson;

#define _LOG_PREFIX(inst) << ""

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


/* "object" is assumed to have passed "IsObject()"
 * check. "member_name_str" is "const char*", "type" is Bool, String,
 * Int, Double, etc.
 *
 * "ret_val" will be assigned the value
 */
#define GET_OBJECT_MEMBER(ret_val, object, member_name_str, type)   \
    do {                                                            \
        const json::Value::ConstMemberIterator itr =                \
            object.FindMember(member_name_str);                     \
        CHECK_NE(itr, object.MemberEnd());                              \
        CHECK(itr->value.Is ## type());                                 \
        ret_val = itr->value.Get ## type();                             \
    } while (0)

namespace blink {


PageModel::PageModel(const char* json_fpath, Webengine* webengine)
    : webengine_(webengine)
{
    std::fstream fs(json_fpath);
    std::stringstream ss;
    ss << fs.rdbuf();
    fs.close();

    const std::string file_contents = ss.str();
    model_json.Parse(file_contents.c_str());

    vlogself(2) << "model_fpath= " << json_fpath;
    CHECK(model_json.IsObject());
}

bool
PageModel::get_main_resource_info(ResourceInfo& res_info)
{
    return get_resource_info(1, res_info);
}

bool
PageModel::get_resource_info(const uint32_t& resInstNum,
                             ResourceInfo& res_info) const
{
    const string resInstNumStr = lexical_cast<string>(resInstNum);

    json::Value::ConstMemberIterator itr =
        model_json["resources"].FindMember(resInstNumStr.c_str());
    CHECK(itr != model_json["resources"].MemberEnd());

    const json::Value& resource = itr->value;
    CHECK(resource.IsObject());

    res_info.instNum = resInstNum;

    GET_OBJECT_MEMBER(res_info.type,
                      resource, "type", String);
    VLOG(2) << "type= [" << res_info.type << "]";

    GET_OBJECT_MEMBER(res_info.part_of_page_loaded_check,
                      resource, "part_of_page_loaded_check", Bool);

    itr = resource.FindMember("req_chain");
    CHECK_NE(itr, resource.MemberEnd());
    CHECK(itr->value.IsArray());
    const json::Value& req_chain_array = itr->value;

    for (json::Value::ConstValueIterator it2 = req_chain_array.Begin();
         it2 != req_chain_array.End(); ++it2)
    {
        const json::Value& req_chain_array_entry = *it2;
        CHECK(req_chain_array_entry.IsObject());

        RequestInfo req_info;
        GET_OBJECT_MEMBER(req_info.method,
                          req_chain_array_entry, "method", String);
        GET_OBJECT_MEMBER(req_info.host,
                          req_chain_array_entry, "host", String);
        GET_OBJECT_MEMBER(req_info.port,
                          req_chain_array_entry, "port", Uint);
        GET_OBJECT_MEMBER(req_info.priority,
                          req_chain_array_entry, "priority", Uint);
        GET_OBJECT_MEMBER(req_info.req_total_size,
                          req_chain_array_entry, "req_total_size", Uint);
        GET_OBJECT_MEMBER(req_info.resp_meta_size,
                          req_chain_array_entry, "resp_meta_size", Uint);
        GET_OBJECT_MEMBER(req_info.resp_body_size,
                          req_chain_array_entry, "resp_body_size", Uint);

        res_info.req_chain.push_back(req_info);
    }
    
    return true;

}

} // end namespace blink
