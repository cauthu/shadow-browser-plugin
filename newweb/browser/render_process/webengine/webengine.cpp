
#include <unistd.h>
#include <string>
#include <iostream>
#include <boost/algorithm/string/join.hpp>

#include <angelscript.h>
#include <angelscript/add_on/scriptstdstring/scriptstdstring.h>

#include "../../../utility/easylogging++.h"
#include "../../../utility/common.hpp"
#include "../../../utility/folly/ScopeGuard.h"

#include "webengine.hpp"

#include "xml/XMLHttpRequest.hpp"

using std::string;
using std::make_pair;
using std::shared_ptr;



static int
MakeRequestID() {
    // start at 1, to help catch bugs where the io process forgot to
    // set the req_id field in its ipc messages sent to us, and thus
    // the field will be zero, and we will not know about it
    static int next_request_id = 1;
    return next_request_id++;
}


namespace blink
{

static void
s_as_MessageCallback(const asSMessageInfo *msg, void *param)
{
	const char *type = "ERR ";
	if( msg->type == asMSGTYPE_WARNING ) 
		type = "WARN";
	else if( msg->type == asMSGTYPE_INFORMATION ) 
		type = "INFO";

	LOG(INFO) << msg->section << " (" << msg->row << ":" << msg->col
              << "): " << type << " : " << msg->message;
}

Webengine::Webengine(
    struct ::event_base* evbase,
    IOServiceIPCClient* ioservice_ipcclient
    )
    : evbase_(evbase)
    , ioservice_ipcclient_(ioservice_ipcclient)
    , as_script_engine_(nullptr)
    , as_script_ctx_(nullptr)
{
    static bool initialized = false;

    // there should be ONLY one webengine per process
    CHECK(!initialized);

    CHECK_NOTNULL(ioservice_ipcclient_);

    ioservice_ipcclient_->set_resource_msg_handler(this);

    _init_angelscript_engine();

    initialized = true;
}

Webengine::~Webengine()
{
    CHECK(as_script_engine_);
    CHECK(as_script_ctx_);

    as_script_ctx_->Release();
    as_script_ctx_ = nullptr;
}

void
Webengine::_init_angelscript_engine()
{
    CHECK(!as_script_engine_);
    as_script_engine_ = asCreateScriptEngine();
    CHECK_NOTNULL(as_script_engine_);

    as_script_ctx_ = as_script_engine_->CreateContext();
    CHECK_NOTNULL(as_script_ctx_);

    LOG(INFO) << "angelscript engine= " << as_script_engine_;

    as_script_engine_->SetMessageCallback(
        asFUNCTION(s_as_MessageCallback), 0, asCALL_CDECL);

    RegisterStdString(as_script_engine_);

    auto rv = 0;

#define REGISTER_MY_METHOD_AS_GLOBAL_FUNC(signature_str, my_impl_func)  \
    do {                                                                \
        rv = as_script_engine_->RegisterGlobalFunction(                 \
                signature_str, asMETHOD(Webengine, my_impl_func),       \
                asCALL_THISCALL_ASGLOBAL, this);                        \
        CHECK_GE(rv, 0);                                                \
    } while (0)

    REGISTER_MY_METHOD_AS_GLOBAL_FUNC(
        "void __msleep(double)", msleep);

    REGISTER_MY_METHOD_AS_GLOBAL_FUNC(
        "void add_elem(uint)", add_elem_to_doc);

    REGISTER_MY_METHOD_AS_GLOBAL_FUNC(
        "void start_timer(uint)", start_timer);

    REGISTER_MY_METHOD_AS_GLOBAL_FUNC(
        "void set_elem_res(uint, uint)", set_elem_res);

    REGISTER_MY_METHOD_AS_GLOBAL_FUNC(
        "void send_xhr(uint)", send_xhr);
}

void
Webengine::loadPage(const char* model_fpath)
{
    // we are probably doing some of what DocumentLoader does

    page_model_.reset(new PageModel(model_fpath));

    resource_fetcher_.reset(
        new ResourceFetcher(this, page_model_.get()));

    // TODO: might want to get this from the page model
    static const uint32_t main_doc_instNum = 7;

    document_.reset(
        new Document(evbase_, main_doc_instNum,
                     this, page_model_.get(), resource_fetcher_.get()));

    document_->load();
}

void
Webengine::request_resource(const PageModel::RequestInfo& req_info,
                            Resource* res)
{
    const auto req_id = MakeRequestID();
    ioservice_ipcclient_->request_resource(
        req_id,
        res->instNum(),
        req_info.host.c_str(),
        req_info.port,
        req_info.req_total_size,
        req_info.resp_meta_size,
        req_info.resp_body_size);
    const auto ret = pending_requests_.insert(make_pair(req_id, res));
    CHECK(ret.second);
}

void
Webengine::msleep(const double msec)
{
    VLOG(2) << "sleep for " << msec << " ms";
    usleep(msec * 1000);
}

void
Webengine::add_elem_to_doc(const uint32_t elemInstNum)
{
    VLOG(2) << "begin, elem:" << elemInstNum;
    CHECK(document_);

    document_->add_elem(elemInstNum);

    VLOG(2) << "done";
}

void
Webengine::start_timer(const uint32_t timerID)
{
    VLOG(2) << "begin, timer:" << timerID;

    PageModel::DOMTimerInfo info;
    const auto rv = page_model_->get_dom_timer_info(timerID, info);
    CHECK(rv);

    DOMTimer::UniquePtr timer(new DOMTimer(evbase_, this, info));

    const auto ret = dom_timers_.insert(make_pair(timerID, std::move(timer)));
    CHECK(ret.second);

    VLOG(2) << "done";
}

void
Webengine::set_elem_res(const uint32_t elemInstNum, const uint32_t resInstNum)
{
    VLOG(2) << "begin, elem:" << elemInstNum << " res:" << resInstNum;
    document_->set_elem_res(elemInstNum, resInstNum);
    VLOG(2) << "done";
}

void
Webengine::send_xhr(const uint32_t instNum)
{
    VLOG(2) << "begin, xhr:" << instNum;

    PageModel::XMLHttpRequestInfo info;
    const auto rv = page_model_->get_xhr_info(instNum, info);
    CHECK(rv);

    XMLHttpRequest::UniquePtr xhr(
        new XMLHttpRequest(this, resource_fetcher_.get(), info));

    xhr->send();

    const auto ret = xhrs_.insert(make_pair(instNum, std::move(xhr)));
    CHECK(ret.second);

    VLOG(2) << "done";
}

void
Webengine::execute_scope(const uint32_t scope_id)
{
    VLOG(2) << "begin, scope:" << scope_id;

    CHECK(!inSet(executed_scope_ids_, scope_id));

    executed_scope_ids_.insert(scope_id);

    std::vector<string> statements;
    const auto found =
        page_model_->get_execution_scope_statements(scope_id, statements);
    CHECK(found);

    VLOG(2) << "scope has " << statements.size() << " statements";

#define __MAIN_FUNC_PROTO "void __main()"

    static const string header(__MAIN_FUNC_PROTO " {");
    static const string footer("}");

    statements.insert(statements.begin(), header);
    statements.push_back(footer);

    const auto codeStr = boost::algorithm::join(statements, "\n");

    // now the source code string is ready


    // Create a new script module
    asIScriptModule *mod = as_script_engine_->GetModule(0, asGM_ALWAYS_CREATE);
    CHECK_NOTNULL(mod);

    SCOPE_EXIT {
        mod->Discard();
        mod = nullptr;
    };

    auto rv = mod->AddScriptSection("script", codeStr.c_str());
    CHECK_GE(rv, 0);

    rv = mod->Build();
    CHECK_GE(rv, 0);

    // we have now compiled the script

    // // Create a context that will execute the script.
    // asIScriptContext *ctx = engine->CreateContext();
    // CHECK_NOTNULL(ctx);

    asIScriptFunction *func =
        as_script_engine_->GetModule(0)->GetFunctionByDecl(__MAIN_FUNC_PROTO);
    CHECK_NOTNULL(func);

    rv = as_script_ctx_->Prepare(func);
    CHECK_GE(rv, 0);

    struct timeval begin_tv;
    auto rv2 = gettimeofday(&begin_tv, nullptr);
    CHECK_EQ(rv2, 0);

    rv = as_script_ctx_->Execute();
    if (rv == asEXECUTION_FINISHED ) {
        VLOG(2) << "scope:" << scope_id << " finished executing";
    } else {
        // The execution didn't finish as we had planned. Determine why.
        if( rv == asEXECUTION_ABORTED ) {
            LOG(FATAL) << "The script was aborted before it could finish";
        }
        else if( rv == asEXECUTION_EXCEPTION ) {
            LOG(FATAL) << "The script ended with an exception.";
        }
    }

    struct timeval done_tv;
    rv2 = gettimeofday(&done_tv, nullptr);
    CHECK_EQ(rv2, 0);

    struct timeval elapsed_tv;
    evutil_timersub(&done_tv, &begin_tv, &elapsed_tv);
    VLOG(2) << "elapsed: " << ((elapsed_tv.tv_sec * 1000) + (int)(((double)elapsed_tv.tv_usec) / 1000)) << " ms";

#undef __MAIN_FUNC_PROTO

    VLOG(2) << "done";
}

void
Webengine::handle_ReceivedResponse(const int& req_id)
{
    CHECK(inMap(pending_requests_, req_id))
        << "we don't know about req_id= " << req_id;
    Resource* resource = pending_requests_[req_id];
    CHECK_NOTNULL(resource);
    // nothing to do: we don't care about response meta data
}

void
Webengine::handle_DataReceived(const int& req_id, const size_t& length)
{
    CHECK(inMap(pending_requests_, req_id))
        << "we don't know about req_id= " << req_id;
    Resource* resource = pending_requests_[req_id];
    CHECK_NOTNULL(resource);

    resource->appendData(length);
}

void
Webengine::handle_RequestComplete(const int& req_id, const bool success)
{
    CHECK(inMap(pending_requests_, req_id))
        << "we don't know about req_id= " << req_id;
    Resource* resource = pending_requests_[req_id];
    CHECK_NOTNULL(resource);

    resource->finish(success);

    // remove the pending request since it's now complete
    pending_requests_.erase(req_id);
}

} // end namespace blink
