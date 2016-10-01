
#include <fstream>
#include <string>
#include <sstream>      // std::stringstream
#include <boost/lexical_cast.hpp>

#include "page_model.hpp"

#include "../../../utility/easylogging++.h"

#include "webengine.hpp"

using std::string;
using std::make_pair;
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
#define GET_OBJECT_MEMBER(ret_val, object, member_name_str, must_exist, type) \
    do {                                                            \
        const json::Value::ConstMemberIterator itr =                \
            (object).FindMember(member_name_str); \
        if (itr != (object).MemberEnd()) {                              \
            CHECK(itr->value.Is ## type());                             \
            ret_val = itr->value.Get ## type();                         \
        } else {                                                        \
            if (must_exist) {                                           \
                logself(FATAL) << "json object does not have member \"" \
                               << member_name_str << "\"";              \
            }                                                           \
        }                                                               \
    } while (0)

namespace blink {


PageModel::PageModel(const char* json_fpath)
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
PageModel::get_main_resource_info(ResourceInfo& res_info) const
{
    const auto rv = get_resource_info(1, res_info);
    CHECK_EQ(res_info.type, "main");
    return rv;
}

bool
PageModel::get_resource_info(const uint32_t& resInstNum,
                             ResourceInfo& info) const
{
    const string resInstNumStr = lexical_cast<string>(resInstNum);

    json::Value::ConstMemberIterator itr =
        model_json["resources"].FindMember(resInstNumStr.c_str());
    CHECK(itr != model_json["resources"].MemberEnd());

    const json::Value& resource = itr->value;
    CHECK(resource.IsObject());

    info.instNum = resInstNum;

    GET_OBJECT_MEMBER(info.type,
                      resource, "type", true, String);
    VLOG(2) << "type= [" << info.type << "]";

    GET_OBJECT_MEMBER(info.part_of_page_loaded_check,
                      resource, "part_of_page_loaded_check", true, Bool);

    if (info.type == "css") {
        double parse_dur_ms = 0;
        GET_OBJECT_MEMBER(parse_dur_ms,
                          resource, "parse_dur_ms", true, Double);
        info.css_parse_dur_ms = (uint32_t)parse_dur_ms;
    } else {
        info.css_parse_dur_ms = 0;
    }

    itr = resource.FindMember("req_chain");
    CHECK_NE(itr, resource.MemberEnd());

    const json::Value& req_chain_array = itr->value;
    CHECK(req_chain_array.IsArray());

    for (json::Value::ConstValueIterator it2 = req_chain_array.Begin();
         it2 != req_chain_array.End(); ++it2)
    {
        const json::Value& req_chain_array_entry = *it2;
        CHECK(req_chain_array_entry.IsObject());

        RequestInfo req_info;
        GET_OBJECT_MEMBER(req_info.method,
                          req_chain_array_entry, "method", true, String);
        GET_OBJECT_MEMBER(req_info.host,
                          req_chain_array_entry, "host", true, String);
        GET_OBJECT_MEMBER(req_info.port,
                          req_chain_array_entry, "port", true, Uint);
        GET_OBJECT_MEMBER(req_info.priority,
                          req_chain_array_entry, "priority", true, Uint);
        GET_OBJECT_MEMBER(req_info.req_total_size,
                          req_chain_array_entry, "req_total_size", true, Uint);
        GET_OBJECT_MEMBER(req_info.resp_meta_size,
                          req_chain_array_entry, "resp_meta_size", true, Uint);
        GET_OBJECT_MEMBER(req_info.resp_body_size,
                          req_chain_array_entry, "resp_body_size", true, Uint);

        info.req_chain.push_back(req_info);
    }
    
    return true;

}

bool
PageModel::get_element_info(const uint32_t& elemInstNum,
                            ElementInfo& info) const
{
    VLOG(2) << "begin, " << elemInstNum;

    const string elemInstNumStr = lexical_cast<string>(elemInstNum);

    json::Value::ConstMemberIterator itr =
        model_json["elements"].FindMember(elemInstNumStr.c_str());
    CHECK(itr != model_json["elements"].MemberEnd());

    const json::Value& element = itr->value;
    CHECK(element.IsObject());

    info.instNum = elemInstNum;

    GET_OBJECT_MEMBER(info.tag,
                      element, "tag", true, String);
    VLOG(2) << "tag= [" << info.tag << "]";

    info.initial_resInstNum = 0;
    GET_OBJECT_MEMBER(info.initial_resInstNum,
                      element, "initial_resInstNum", false, Uint);

    // populate specific element-specific stuff

    if (info.tag == "script") {
        GET_OBJECT_MEMBER(info.exec_async,
                          element, "asyncExec", true, Bool);
        GET_OBJECT_MEMBER(info.exec_immediately,
                          element, "exec_immediately", true, Bool);
        GET_OBJECT_MEMBER(info.is_parser_blocking,
                          element, "is_parser_blocking", true, Bool);
        GET_OBJECT_MEMBER(info.run_scope_id,
                          element, "run_scope_id", true, Uint);
    } else if (info.tag == "link") {
        GET_OBJECT_MEMBER(info.rel,
                          element, "rel", true, String);
        GET_OBJECT_MEMBER(info.is_blocking_css,
                          element, "is_blocking_css", true, Bool);
    }

    {
        vlogself(2) << "get the event_handling_scopes";

        json::Value::ConstMemberIterator itr =
            element.FindMember("event_handling_scopes");
        if (itr != element.MemberEnd()) {
            const json::Value& pair_array = itr->value;
            CHECK(pair_array.IsArray());

            for (json::Value::ConstValueIterator it2 = pair_array.Begin();
                 it2 != pair_array.End(); ++it2)
            {
                const json::Value& pair = *it2;
                CHECK(pair.IsArray());
                CHECK_EQ(pair.Size(), 2);

                CHECK(pair[0].IsString());
                const auto event_name = pair[0].GetString();

                CHECK(pair[1].IsUint());
                const auto scope_id = pair[1].GetUint();

                vlogself(2) << "event name= " << event_name
                            << " scope id= " << scope_id;

                info.event_handling_scopes.push_back(
                    make_pair(event_name, scope_id));
            }
        } else {
            vlogself(2) << "it has none";
        }
    }

    VLOG(2) << "done";

    return true;
}

void
PageModel::get_main_html_element_byte_offsets(std::vector<std::pair<size_t, uint32_t> >& elem_locs) const
{
    json::Value::ConstMemberIterator itr =
        model_json["main_html"].FindMember("element_byte_offsets");
    CHECK(itr != model_json["main_html"].MemberEnd());

    const json::Value& pair_array = itr->value;
    CHECK(pair_array.IsArray());

    for (json::Value::ConstValueIterator it2 = pair_array.Begin();
         it2 != pair_array.End(); ++it2)
    {
        const json::Value& pair = *it2;
        CHECK(pair.IsArray());
        CHECK_EQ(pair.Size(), 2);

        CHECK(pair[0].IsUint());
        const auto byte_offset = pair[0].GetUint();

        CHECK(pair[1].IsUint());
        const auto elemInstNum = pair[1].GetUint();

        elem_locs.push_back(make_pair(byte_offset, elemInstNum));
    }
}

uint32_t
PageModel::get_element_initial_resInstNum(const uint32_t& elemInstNum) const
{
    vlogself(2) << "begin, elemInstNum= " << elemInstNum;

    const string elemInstNumStr = lexical_cast<string>(elemInstNum);

    json::Value::ConstMemberIterator itr =
        model_json["elements"].FindMember(elemInstNumStr.c_str());
    CHECK(itr != model_json["elements"].MemberEnd());

    const json::Value& element = itr->value;
    CHECK(element.IsObject());

    uint32_t resInstNum = 0;
    GET_OBJECT_MEMBER(resInstNum,
                      element, "initial_resInstNum", false, Uint);

    vlogself(2) << "done";

    return resInstNum;
}

bool
PageModel::get_execution_scope_statements(uint32_t scope_id,
                                          std::vector<std::string>& statements) const
{
    vlogself(2) << "begin, scope id= " << scope_id;

    const string scopeIdStr = lexical_cast<string>(scope_id);

    json::Value::ConstMemberIterator itr =
        model_json["exec_scopes"].FindMember(scopeIdStr.c_str());
    CHECK(itr != model_json["exec_scopes"].MemberEnd())
        << "page model doesn't contain scope: " << scope_id;

    const json::Value& statement_array = itr->value;
    CHECK(statement_array.IsArray());

    for (json::Value::ConstValueIterator it2 = statement_array.Begin();
         it2 != statement_array.End(); ++it2)
    {
        const json::Value& statement = *it2;
        CHECK(statement.IsString());

        statements.push_back(statement.GetString());
    }

    vlogself(2) << "done";
    return true;
}

bool
PageModel::get_dom_timer_info(uint32_t timerID,
                              DOMTimerInfo& info) const
{
    VLOG(2) << "begin, timer:" << timerID;

    const string timerIDStr = lexical_cast<string>(timerID);

    json::Value::ConstMemberIterator itr =
        model_json["timers"].FindMember(timerIDStr.c_str());
    CHECK(itr != model_json["timers"].MemberEnd());

    const json::Value& timer = itr->value;
    CHECK(timer.IsObject());

    info.timerID = timerID;

    GET_OBJECT_MEMBER(info.singleShot,
                      timer, "singleShot", true, Bool);

    GET_OBJECT_MEMBER(info.interval_ms,
                      timer, "interval_ms", true, Uint);

    {
        vlogself(2) << "get the timer fired scopes";

        json::Value::ConstMemberIterator itr =
            timer.FindMember("fired_scope_ids");
        CHECK_NE(itr, timer.MemberEnd());

        const json::Value& scope_id_array = itr->value;
        CHECK(scope_id_array.IsArray());

        for (json::Value::ConstValueIterator it2 = scope_id_array.Begin();
             it2 != scope_id_array.End(); ++it2)
        {
            const json::Value& scope_id = *it2;
            CHECK(scope_id.IsUint());

            info.fired_scope_ids.push_back(scope_id.GetUint());
        }
    }

    return true;
}

} // end namespace blink
