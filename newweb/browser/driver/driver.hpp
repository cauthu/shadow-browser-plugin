#ifndef driver_hpp
#define driver_hpp

#include <event2/event.h>
#include <boost/random.hpp>
#include <boost/generator_iterator.hpp>

#include "../../utility/stream_channel.hpp"
#include "../../utility/generic_message_channel.hpp"
#include "../../utility/ipc/generic_ipc_channel.hpp"
#include "../../utility/object.hpp"

#include "utility/ipc/renderer/gen/combined_headers"

/*
 * this is akin to the chrome driver: it uses ipc to control the
 * renderer process, e.g., instructs the render to load pages, etc.
 *
 * the driver also OPTIONALLY talks to the transport proxy to set it
 * up for each page load.
 *
 * the driver implement is spread across multiple driver_<...>.cpp
 * files just to be manageable and so each can use its own macros,
 * etc.

 *
 *
 * the default/common state transitions are:
 *
 * 1. reset renderer
 *
 * 2. sleep for random "think time" amount if this is not the first load
 *
 * 3. (if using proxy then tell it to set_auto_start_on_next_send, and) start loading page
 * --> if timed out, go back to step 1.
 * --> if success go to 5.
 *
 * 4. wait for more requests that might be sent: whenever there are
 * pending requests, we are patient and continue to wait. when a
 * request is finished (closed or success), if there is no more
 * pending requests, and the dom load event has fired, then we start
 * timer to wait a few seconds before fully stopping this page
 * load. while we wait, any new requests will reset the waiting logic
 *
 * --> when done go back to 1.
 * 
 */




class Driver : public Object
{
public:

    typedef std::unique_ptr<Driver, Destructor> UniquePtr;

    explicit Driver(struct event_base*,
                    const std::string& page_models_list_file,
                    const bool& sequential_page_selection,
                    const std::string& browser_proxy_mode,
                    const uint16_t tproxy_ipc_port,
                    const uint16_t renderer_ipc_port);

private:

    virtual ~Driver();


    //////////////

    /*  for interacting with tproxy  */

    void _tproxy_on_ipc_msg(myipc::GenericIpcChannel*, uint8_t type,
                            uint16_t len, const uint8_t *data);
    void _tproxy_on_ipc_ch_status(myipc::GenericIpcChannel*,
                                  myipc::GenericIpcChannel::ChannelStatus);

#if 0
    void _tproxy_maybe_establish_tunnel();
    void _tproxy_on_establish_tunnel_resp(myipc::GenericIpcChannel::RespStatus,
                                   uint16_t len, const uint8_t* buf);
#endif

    void _tproxy_stop_defense(const bool& right_now);
    void _tproxy_on_stop_defense_resp(myipc::GenericIpcChannel::RespStatus,
                                   uint16_t len, const uint8_t* buf);

    ///////////

    /*  for interacting with renderer  */

    void _renderer_on_ipc_msg(myipc::GenericIpcChannel*, uint8_t type,
                              uint16_t len, const uint8_t *data);
    void _renderer_on_ipc_ch_status(myipc::GenericIpcChannel*,
                                    myipc::GenericIpcChannel::ChannelStatus);

    void _renderer_reset();
    void _renderer_on_reset_resp(myipc::GenericIpcChannel::RespStatus,
                                 uint16_t len, const uint8_t* buf);
    void _renderer_load_page();
    void _renderer_on_load_page_resp(myipc::GenericIpcChannel::RespStatus,
                                     uint16_t len, const uint8_t* buf);

    void _tproxy_set_auto_start_defense_on_next_send();
    void _tproxy_on_set_auto_start_defense_on_next_send_resp(myipc::GenericIpcChannel::RespStatus,
                                                          uint16_t, const uint8_t* buf);

    void _renderer_handle_PageLoaded(const myipc::renderer::messages::PageLoadedMsg*);
    void _renderer_handle_PageLoadFailed(const myipc::renderer::messages::PageLoadFailedMsg*);
    void _renderer_handle_RequestWillBeSent(
        const myipc::renderer::messages::RequestWillBeSentMsg* msg);
    void _renderer_handle_RequestFinished(
        const myipc::renderer::messages::RequestFinishedMsg* msg);

    //////////

    void _on_page_load_timeout(Timer*);
    void _on_wait_for_more_requests_timer_fired(Timer*);
    void _on_think_time_timer_fired(Timer*);

    void _read_page_models_file(const std::string&);

    /////////

    struct event_base* evbase_;

    myipc::GenericIpcChannel::UniquePtr renderer_ipc_ch_;
    myipc::GenericIpcChannel::UniquePtr tproxy_ipc_ch_;

    /* list of <page name, file path to the page model> pairs */
    std::vector<std::pair<std::string, std::string> > page_models_;
    const bool sequential_page_selection_;
    bool using_tproxy_;
    bool tproxy_ipc_ch_ready_;
    const std::string browser_proxy_mode_;
    std::unique_ptr<boost::variate_generator<boost::mt19937, boost::uniform_int<> > > page_model_rand_idx_gen_;
    std::unique_ptr<boost::variate_generator<boost::mt19937, boost::uniform_real<> > > think_time_rand_gen_;

    enum class State
    {
        INITIAL,

            RESET_RENDERER,
            DONE_RESET_RENDERER,
#if 0
            ESTABLISH_TPROXY_TUNNEL,
            DONE_ESTABLISH_TPROXY_TUNNEL,
            SET_TPROXY_AUTO_START,
            DONE_SET_TPROXY_AUTO_START,
#endif

            LOADING_PAGE,

        /* "grace period" is probably not best choice of words, but
         * basically webpages can fire "load" event but still
         * afterwards generate a lot of network requests, so we wait
         * for a little bit to catch (some of) these requests before
         * reporting page load result
         */
            WAIT_FOR_MORE_REQUESTS_AFTER_DOM_LOAD_EVENT,

            THINKING,
    } state_;

    Timer::UniquePtr page_load_timeout_timer_;
    Timer::UniquePtr wait_for_more_requests_timer_;
    Timer::UniquePtr think_time_timer_;

    enum class PageLoadStatus {
        NONE = 0,
        PENDING,
        OK,
        FAILED,
        TIMEDOUT
    };
    static const char* s_page_load_status_to_string(const PageLoadStatus&);

    uint32_t loadnum_;

    struct OnePageLoadInfo
    {
        uint32_t page_model_idx_;
        uint64_t load_start_timepoint_;
        uint64_t DOM_load_event_fired_timepoint_;
        uint32_t num_reqs_;
        uint32_t num_succes_reqs_;
        uint32_t num_failed_reqs_;
        uint32_t num_after_DOM_load_event_reqs_;
        int32_t num_pending_reqs_;

        PageLoadStatus page_load_status_;
        uint32_t ttfb_ms_;

        std::set<uint32_t> forced_load_resInstNums_;
    } this_page_load_info_;

    // so that we can do sequential page selection
    uint32_t prev_page_model_idx_ = 0;

    void _do_start_thinking_or_loading();
    void _start_thinking();
    void _report_result();
    void _reset_this_page_load_info();
};

#endif /* end driver_hpp */
