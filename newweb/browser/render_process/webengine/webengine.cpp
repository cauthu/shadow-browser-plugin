
#include <unistd.h>
#include <string>
#include <iostream>
#include <boost/algorithm/string/join.hpp>

#include <angelscript.h>
#include <angelscript/add_on/scriptstdstring/scriptstdstring.h>

#include "../../../utility/easylogging++.h"
#include "../../../utility/common.hpp"

#include "webengine.hpp"


using std::string;
using std::make_pair;


static asIScriptEngine* s_as_script_engine = nullptr;


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

/* sleep milliseconds */
static void
s_as_msleep(uint32_t msec)
{
    VLOG(3) << "about to sleep for " << msec << " ms";
    usleep(msec * 1000);
}

static void
s_as_addElement(uint32_t instNum)
{
    CHECK(0) << "todo";
}

static void
s_as_print(const string& s)
{
    LOG(INFO) << "script is printing [" << s << "]";
}

double mymulti(int i, double j)
{
  return i * j;
}

double myadd(int i, double j)
{
  return i + j;
}

Webengine::Webengine(IOServiceIPCClient* ioservice_ipcclient)
    : ioservice_ipcclient_(ioservice_ipcclient)
{
    static bool initialized = false;

    // there should be ONLY one webengine per process
    CHECK(!initialized);

    CHECK_NOTNULL(ioservice_ipcclient_);

    ioservice_ipcclient_->set_resource_msg_handler(this);

    _init_angelscript_engine();

    initialized = true;
}

void
Webengine::_init_angelscript_engine()
{
    CHECK_EQ(s_as_script_engine, nullptr);
    s_as_script_engine = asCreateScriptEngine();
    CHECK_NOTNULL(s_as_script_engine);

    LOG(INFO) << "angelscript engine= " << s_as_script_engine;

    asIScriptEngine* engine = s_as_script_engine;

    engine->SetMessageCallback(
        asFUNCTION(s_as_MessageCallback), 0, asCALL_CDECL);

    RegisterStdString(engine);

    auto rv = 0;

#define REGISTER_GLOBAL_AS_FUNC(signature_str, our_impl_func)           \
    do {                                                                \
        rv = engine->RegisterGlobalFunction(                            \
            signature_str, asFUNCTION(our_impl_func), asCALL_CDECL);    \
        CHECK_GE(rv, 0);                                                \
    } while (0)

    REGISTER_GLOBAL_AS_FUNC(
        "void msleep(uint msec)", s_as_msleep);
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
        new Document(main_doc_instNum,
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
Webengine::msleep(const uint32_t ms)
{
    s_as_msleep(ms);
}

void
Webengine::execute_scope(const uint32_t scope_id)
{
    VLOG(2) << "begin, scope_id= " << scope_id;

    CHECK(!inSet(executed_scope_ids_, scope_id));

    executed_scope_ids_.insert(scope_id);

    std::vector<string> statements;
    page_model_->get_execution_scope_statements(scope_id, statements);

    VLOG(2) << "scope has " << statements.size() << " statements";

    static const string header("void __main() {");
    static const string footer("}");

    statements.insert(statements.begin(), header);
    statements.push_back(footer);

    const auto codeStr = boost::algorithm::join(statements, "\n");

    /// now the code string is ready

    asIScriptEngine* engine = s_as_script_engine;
    CHECK_NOTNULL(engine);

    asIScriptModule *mod = engine->GetModule(0, asGM_ALWAYS_CREATE);
    CHECK_NOTNULL(mod);

    auto r = mod->AddScriptSection("script", codeStr.c_str());
    CHECK_GE(r, 0);

    r = mod->Build();
    CHECK_GE(r, 0);

    // Create a context that will execute the script.
    asIScriptContext *ctx = engine->CreateContext();
    CHECK_NOTNULL(ctx);

    asIScriptFunction *func =
        engine->GetModule(0)->GetFunctionByDecl("void __main()");
    CHECK_NOTNULL(func);

    r = ctx->Prepare(func);
    CHECK_GE(r, 0);

    r = ctx->Execute();
    if (r == asEXECUTION_FINISHED ) {
        VLOG(2) << "scope finished executing";
    } else {
        // The execution didn't finish as we had planned. Determine why.
        if( r == asEXECUTION_ABORTED ) {
            LOG(FATAL) << "The script was aborted before it could finish";
        }
        else if( r == asEXECUTION_EXCEPTION ) {
            LOG(FATAL) << "The script ended with an exception.";
        }
    }

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
