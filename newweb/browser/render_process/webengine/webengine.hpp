#ifndef webengine_hpp
#define webengine_hpp

#include <memory>
#include <angelscript.h>

#include "../../../utility/object.hpp"
#include "../../../utility/timer.hpp"

#include "../interfaces.hpp"
#include "../ipc_io_service.hpp"
#include "../ipc_renderer.hpp"

#include "page_model.hpp"

#include "dom/Document.hpp"
#include "fetch/Resource.hpp"
#include "fetch/ResourceFetcher.hpp"
#include "frame/DOMTimer.hpp"
#include "xml/XMLHttpRequest.hpp"

/*
 * from the html processing model
 * https://html.spec.whatwg.org/multipage/webappapis.html#event-loop-processing-model,
 * the event loop deals with "tasks"
 *
 * the browser has one or more task queue, where tasks can be to
 * handle ipc messages from io service (e.g., "DataReceived",
 * "RequestComplete", etc.) as well as handle timeout callbacks
 * (including DOMTimer timeouts).
 *
 * after each task is run, the browser should run the "Update the
 * rendering" step
 *
 * we are not exactly implementing that processing model; we will
 * approximate this by trying to run a render update scope in
 * _do_end_of_task_work(), which we will call after: (1) handling ipc
 * messages, and (2) after each DOMTimer fires (make DOMTimer a friend
 * class so it can call our _do_end_of_task_work())
 *
 */

namespace blink
{

class Webengine : public Object
                , public ResourceMsgHandler
                , public DriverMsgHandler
{
public:
    typedef std::unique_ptr<Webengine, /*folly::*/Destructor> UniquePtr;

    explicit Webengine(struct ::event_base*,
                       IOServiceIPCClient*,
                       IPCServer*
        );

    /* ------- send messages to io service --------- */
    /* will send a request to the io process, and will notify the
     * Resource response/data
     */
    void ioservice_request_resource(const PageModel::RequestInfo& req_info,
                                    Resource* res);

    /* ------- send messages to client of renderer ipc --------- */
    void renderer_notify_RequestWillBeSent(const uint32_t& resInstNum,
                                           const uint32_t& reqChainIdx);
    void renderer_notify_RequestFinished(const uint32_t& resInstNum,
                                         const uint32_t& reqChainIdx,
                                         const bool& success);

    /* implement ResourceMsgHandler interface */
    virtual void handle_ReceivedResponse(const int& req_id,
                                         const uint64_t& first_byte_time_ms) override;
    virtual void handle_DataReceived(const int& req_id, const size_t& length) override;
    virtual void handle_RequestComplete(const int& req_id, const bool success) override;

    /* DriverMsgHandler interface */
    void handle_LoadPage(const uint32_t load_id, const char* model_fpath) override;
    void handle_Reset() override;

    void msleep(const double ms);

    void execute_scope(const uint32_t scope_id);

  void finishedParsing();
    void checkCompleted();
    struct ::event_base* evbase() { return evbase_; }

protected:

    virtual ~Webengine();

    //////

    void _init_angelscript_engine();
    void checkCompleted_timer_fired(Timer*);
    void _reset();
    void _main_resource_failed();

    friend class DOMTimer;
    friend class Document;

    void _do_end_of_task_work();

    void _maybe_load_unloaded_resources();

    // reset to prepare for new page load, including clearing any
    // state that might be existing from the last page load
    void _reset_loading_state();

    void maybe_sched_INITIAL_render_update_scope();

    // these methods are for execution code to call
    void add_elem_to_doc(const uint32_t elemInstNum);
    void sched_render_update_scope(const uint32_t scope_id);
    void start_timer(const uint32_t timerID);
    void cancel_timer(const uint32_t timerID);
    void set_elem_res(const uint32_t elemInstNum, const uint32_t resInstNum);
    void send_xhr(const uint32_t xhrInstNum);

    /////

    struct ::event_base* evbase_;
    IOServiceIPCClient* ioservice_ipcclient_;
    IPCServer* renderer_ipcserver_;

    asIScriptEngine* as_script_engine_;
    asIScriptContext* as_script_ctx_;

    /* state will be "page_loading" until the "load" event is
     * fired. once the page's load event has fired, we are back to
     * idle
     */
    enum class State {
        IDLE, PAGE_LOADING,
            } state_;

    uint64_t start_load_time_ms_;

    uint32_t current_load_id_;

    /* map from the request id (for IPC!! not the resInstNum) that we
     * generate to the resource for which we're requesting. this is
     * like chrome's resource_disptacher's "pending_requests_"
     *
     * the resource pointers are only shallow pointers
     */
    std::map<int, Resource*> pending_requests_;

    PageModel::UniquePtr page_model_;
    ResourceFetcher::UniquePtr resource_fetcher_;
    Document::UniquePtr document_;

    uint32_t initial_render_tree_update_scope_id_ = 0;

    /* timer to do the checkCompleted() on our own stack, not while
     * being called back by some resource finish for example
     */
    Timer::UniquePtr checkCompleted_timer_;

    uint32_t scheduled_render_tree_update_scope_id_;

    /* these are ids of the execution scopes that have executed, and
     * therefore cannot be executed again. we will crash on such
     * attempt
     */
    std::set<uint32_t> executed_scope_ids_;

    std::map<uint32_t, DOMTimer::UniquePtr> dom_timers_;
    std::map<uint32_t, XMLHttpRequest::UniquePtr> xhrs_;
};

}

#endif /* webengine_hpp */
