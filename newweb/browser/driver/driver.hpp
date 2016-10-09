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
 * the driver also talks to the transport proxy to set it up for each
 * page load.
 *
 * the driver implement is spread across multiple driver_<...>.cpp
 * files just to be manageable and so each can use its own macros,
 * etc.

 *
 *
 * the default/common state transitions are:
 *
 * * when renderer ipc channel is ready, tell renderer to reset
 *
 * * when that is done, tell tproxy to establish tunnel (with force reconnect)
 *
 * * when that is done, tell tproxy to set auto start defense
 *
 * * when that is done, tell renderer to load page
 *
 */

class Driver : public Object
{
public:

    typedef std::unique_ptr<Driver, Destructor> UniquePtr;

    explicit Driver(struct event_base*,
                    const std::string& page_models_list_file,
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

    void _tproxy_maybe_establish_tunnel();
    void _tproxy_on_establish_tunnel_resp(myipc::GenericIpcChannel::RespStatus,
                                   uint16_t len, const uint8_t* buf);

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
    void _renderer_handle_RequestWillBeSent(
        const myipc::renderer::messages::RequestWillBeSentMsg* msg);
    void _renderer_handle_RequestFinished(
        const myipc::renderer::messages::RequestFinishedMsg* msg);

    //////////

    void _on_page_load_timeout(Timer*);
    void _on_grace_period_timer_fired(Timer*);
    void _on_think_time_timer_fired(Timer*);

    void _read_page_models_file(const std::string&);

    /////////

    struct event_base* evbase_;

    myipc::GenericIpcChannel::UniquePtr renderer_ipc_ch_;
    myipc::GenericIpcChannel::UniquePtr tproxy_ipc_ch_;

    /* list of <page name, file path to the page model> pairs */
    std::vector<std::pair<std::string, std::string> > page_models_;
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
            ESTABLISH_TPROXY_TUNNEL,
            DONE_ESTABLISH_TPROXY_TUNNEL,
            SET_TPROXY_AUTO_START,
            DONE_SET_TPROXY_AUTO_START,

            LOADING_PAGE,

        /* "grace period" is probably not best choice of words, but
         * basically webpages can fire "load" event but still
         * afterwards generate a lot of network requests, so we wait
         * for a little bit to catch (some of) these requests before
         * reporting page load result
         */
            GRACE_PERIOD_AFTER_DOM_LOAD_EVENT,

            THINKING,
    } state_;

    Timer::UniquePtr page_load_timeout_timer_;
    Timer::UniquePtr grace_period_timer_;
    Timer::UniquePtr think_time_timer_;

    enum class PageLoadStatus {
        PENDING = 0,
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
        uint32_t num_post_DOM_load_event_reqs_;

        PageLoadStatus page_load_status_;
        uint32_t ttfb_ms_;
    } this_page_load_info_;

    void _report_result(const PageLoadStatus&,
                        const uint32_t&);
    void _reset_this_page_load_info();
};

#endif /* end driver_hpp */
