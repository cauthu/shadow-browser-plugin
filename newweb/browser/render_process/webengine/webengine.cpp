
#include <unistd.h>
#include <string>
#include <iostream>
#include <boost/algorithm/string/join.hpp>
#include <boost/bind.hpp>

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
    CHECK_LT(next_request_id, 0xffffff);
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
    IOServiceIPCClient* ioservice_ipcclient,
    IPCServer* renderer_ipcserver
    )
    : evbase_(evbase)
    , ioservice_ipcclient_(ioservice_ipcclient)
    , renderer_ipcserver_(renderer_ipcserver)
    , as_script_engine_(nullptr)
    , as_script_ctx_(nullptr)
    , start_load_time_ms_(0)
    , current_load_id_(0)
    , state_(State::IDLE)
    , scheduled_render_tree_update_scope_id_(0)
    , checkCompleted_timer_(
        new Timer(evbase_, true,
                  boost::bind(&Webengine::checkCompleted_timer_fired, this, _1)))
{
    static bool initialized = false;

    // there should be ONLY one webengine per process
    CHECK(!initialized);

    CHECK_NOTNULL(ioservice_ipcclient_);

    ioservice_ipcclient_->set_resource_msg_handler(this);
    renderer_ipcserver_->set_driver_msg_handler(this);

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
        "void sched_render_update_scope(uint)", sched_render_update_scope);

    REGISTER_MY_METHOD_AS_GLOBAL_FUNC(
        "void start_timer(uint)", start_timer);

    REGISTER_MY_METHOD_AS_GLOBAL_FUNC(
        "void cancel_timer(uint)", cancel_timer);

    REGISTER_MY_METHOD_AS_GLOBAL_FUNC(
        "void set_elem_res(uint, uint)", set_elem_res);

    REGISTER_MY_METHOD_AS_GLOBAL_FUNC(
        "void send_xhr(uint)", send_xhr);
}

void
Webengine::_reset()
{
    LOG(INFO) << "reset webengine...";
    ioservice_ipcclient_->send_ResetSession();
    _reset_loading_state();
    LOG(INFO) << "done";
}

void
Webengine::handle_Reset()
{
    _reset();
}

void
Webengine::handle_LoadPage(const uint32_t load_id,
                           const char* model_fpath)
{
    // we are probably doing some of what DocumentLoader does

    // tell io service to drop whatever connections, requests, etc. it
    // might have currently

    LOG(INFO) << "start loading page, model [" << model_fpath << "] "
              << "load id= " << load_id;
    CHECK_NE(load_id, current_load_id_);
    CHECK_GT(load_id, 0);

    /* maybe _reset() ?*/
    // _reset();

    page_model_.reset(new PageModel(model_fpath));

    resource_fetcher_.reset(
        new ResourceFetcher(this, page_model_.get()));

    // TODO: might want to get this from the page model
    static const uint32_t main_doc_instNum = 7;

    document_.reset(
        new Document(evbase_, main_doc_instNum,
                     this, page_model_.get(), resource_fetcher_.get()));

    document_->load();

    start_load_time_ms_ = common::gettimeofdayMs();
    current_load_id_ = load_id;
    state_ = State::PAGE_LOADING;
}

void
Webengine::_reset_loading_state()
{
    document_.reset();
    page_model_.reset();
    state_ = State::IDLE;
    resource_fetcher_.reset();
    dom_timers_.clear();
    xhrs_.clear();
    executed_scope_ids_.clear();
    pending_requests_.clear();
    checkCompleted_timer_->cancel();
    scheduled_render_tree_update_scope_id_ = 0;
    start_load_time_ms_ = 0;
    current_load_id_ = 0;
}

void
Webengine::ioservice_request_resource(const PageModel::RequestInfo& req_info,
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
Webengine::renderer_notify_RequestWillBeSent(const uint32_t& resInstNum,
                                           const uint32_t& reqChainIdx)
{
    if (!renderer_ipcserver_) {
        return;
    }
    renderer_ipcserver_->send_RequestWillBeSent(resInstNum, reqChainIdx);
}

void
Webengine::renderer_notify_RequestFinished(const uint32_t& resInstNum,
                                         const uint32_t& reqChainIdx,
                                         const bool& success)
{
    if (!renderer_ipcserver_) {
        return;
    }
    renderer_ipcserver_->send_RequestFinished(
        resInstNum, reqChainIdx, success);
}

void
Webengine::_main_resource_failed()
{
    LOG(WARNING) << "main resource failed to load, so reset and notify user";
    const auto load_id = current_load_id_;
    _reset();
    renderer_ipcserver_->send_PageLoadFailed(load_id);
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
Webengine::sched_render_update_scope(const uint32_t scope_id)
{
    VLOG(2) << "begin, scope:" << scope_id;

    CHECK_GT(scope_id, 0);
    CHECK_NE(scheduled_render_tree_update_scope_id_, scope_id) << scope_id;
    if (scheduled_render_tree_update_scope_id_) {
        LOG(WARNING)
            << "overwriting currently scheduled render update scope id "
            << scheduled_render_tree_update_scope_id_
            << " with new one " << scope_id;
    }

    scheduled_render_tree_update_scope_id_ = scope_id;

    VLOG(2) << "done";
}

void
Webengine::cancel_timer(const uint32_t timerID)
{
    VLOG(2) << "begin, timer:" << timerID;

    std::map<uint32_t, DOMTimer::UniquePtr>::iterator it =
        dom_timers_.find(timerID);
    if (it != dom_timers_.end()) {
        DOMTimer::UniquePtr timer = std::move(it->second);
        CHECK_NOTNULL(timer.get());
        timer->cancel();
    } else {
        LOG(WARNING) << "timer:" << timerID << " does not (yet) exist";
    }

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
    VLOG(2) << "begin, scope:" << scope_id << ":";

    const auto ret = executed_scope_ids_.insert(scope_id);
    CHECK(ret.second) << "scope:" << scope_id << " has already been executed";

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

    const auto begin_time_ms = common::gettimeofdayMs(nullptr);
    CHECK_GT(begin_time_ms, 0);

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

    const auto done_time_ms = common::gettimeofdayMs(nullptr);
    CHECK_GE(done_time_ms, begin_time_ms);

    VLOG(2) << "elapsed: " << (done_time_ms - begin_time_ms) << " ms";

#undef __MAIN_FUNC_PROTO

    VLOG(2) << "done";
}

void
Webengine::handle_ReceivedResponse(const int& req_id,
                                   const uint64_t& first_byte_time_ms)
{
    auto it = pending_requests_.find(req_id);
    if (it == pending_requests_.end()) {
        // this can happen because there's a race between our
        // clearing/cancelling of pending request and io service's
        // notifying us about the request
        return;
    }

    Resource* resource = pending_requests_[req_id];
    CHECK_NOTNULL(resource);

    resource->receivedResponseMeta(first_byte_time_ms);

    //////
    _do_end_of_task_work();
}

void
Webengine::handle_DataReceived(const int& req_id, const size_t& length)
{
    auto it = pending_requests_.find(req_id);
    if (it == pending_requests_.end()) {
        // this can happen because there's a race between our
        // clearing/cancelling of pending request and io service's
        // notifying us about the request
        return;
    }

    Resource* resource = it->second;
    CHECK_NOTNULL(resource);

    resource->appendData(length);

    //////
    _do_end_of_task_work();
}

void
Webengine::handle_RequestComplete(const int& req_id, const bool success)
{
    auto it = pending_requests_.find(req_id);
    if (it == pending_requests_.end()) {
        // this can happen because there's a race between our
        // clearing/cancelling of pending request and io service's
        // notifying us about the request
        return;
    }

    Resource* resource = pending_requests_[req_id];
    CHECK_NOTNULL(resource);

    // remove the pending request since it's now complete
    pending_requests_.erase(it);

    if (!success && (1 == resource->instNum())) {
        _main_resource_failed();
    } else {
        resource->finish(success);

        //////
        _do_end_of_task_work();
    }
}

void
Webengine::finishedParsing()
{
  checkCompleted();
}

void
Webengine::checkCompleted()
{
    VLOG(2) << "begin";

    if (state_ != State::PAGE_LOADING) {
        VLOG(2) << "we're not loading page";
        return;
    }

    if (resource_fetcher_->requestCount()) {
        VLOG(2) << "resource fetcher is not done";
        return;
    }

    if (checkCompleted_timer_->is_running()) {
        VLOG(2) << "already scheduled";
        return;
    }

    static const uint32_t zero_ms = 0;
    checkCompleted_timer_->start(zero_ms);
    VLOG(2) << "done";
    return;
}

void
Webengine::checkCompleted_timer_fired(Timer*)
{
    VLOG(2) << "begin";
    if (state_ != State::PAGE_LOADING) {
        VLOG(2) << "we're not loading page";
        return;
    }

    CHECK_NOTNULL(document_.get());
    if (document_->parsing()) {
        VLOG(2) << "document still parsing";
        return;
    }

    if (resource_fetcher_->requestCount()) {
        VLOG(2) << "resource fetcher is not done";
        return;
    }

    document_->setReadyState(Document::ReadyState::Complete);
    document_->implicitClose();

    state_ = State::IDLE;

    const auto ttfb_ms = document_->first_byte_time_ms() - start_load_time_ms_;

    renderer_ipcserver_->send_PageLoaded(current_load_id_, ttfb_ms);

    VLOG(2) << "done";
}

void
Webengine::_do_end_of_task_work()
{
    if (scheduled_render_tree_update_scope_id_) {
        const auto scope_id = scheduled_render_tree_update_scope_id_;
        // clear the scheduled scope id before executing
        scheduled_render_tree_update_scope_id_ = 0;
        VLOG(2) << "execute render tree update scope:" << scope_id;
        execute_scope(scope_id);
    }
}

void
Webengine::maybe_sched_INITIAL_render_update_scope()
{
    VLOG(2) << "begin";

    CHECK_EQ(scheduled_render_tree_update_scope_id_, 0);

    scheduled_render_tree_update_scope_id_ =
        page_model_->get_initial_render_tree_update_scope_id();

    // it's ok for the initial scope id to be zero, e.g., because it's
    // part (inner scope) of another scope

    VLOG(2) << "done, scheduled scope:" << scheduled_render_tree_update_scope_id_;
}

} // end namespace blink
