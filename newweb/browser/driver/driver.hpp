#ifndef driver_hpp
#define driver_hpp

#include <event2/event.h>

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
 */

class Driver : public Object
{
public:

    typedef std::unique_ptr<Driver, Destructor> UniquePtr;

    explicit Driver(struct event_base*,
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

    void _tproxy_establish_tunnel();
    void _tproxy_on_establish_tunnel_resp(myipc::GenericIpcChannel::RespStatus,
                                   uint16_t len, const uint8_t* buf);


    ///////////

    /*  for interacting with renderer  */

    void _renderer_on_ipc_msg(myipc::GenericIpcChannel*, uint8_t type,
                              uint16_t len, const uint8_t *data);
    void _renderer_on_ipc_ch_status(myipc::GenericIpcChannel*,
                                    myipc::GenericIpcChannel::ChannelStatus);

    void _renderer_load();
    void _renderer_on_load_resp(myipc::GenericIpcChannel::RespStatus,
                       uint16_t len, const uint8_t* buf);

    void _tproxy_set_auto_start_defense_on_next_send();
    void _tproxy_on_set_auto_start_defense_on_next_send_resp(myipc::GenericIpcChannel::RespStatus,
                                                          uint16_t, const uint8_t* buf);

    void _renderer_maybe_start_load();

    void _renderer_handle_PageLoaded(const myipc::renderer::messages::PageLoadedMsg*);

    //////////

    void _on_think_time_timer_fired(Timer*);


    /////////

    struct event_base* evbase_;

    myipc::GenericIpcChannel::UniquePtr renderer_ipc_ch_;
    myipc::GenericIpcChannel::UniquePtr tproxy_ipc_ch_;

    enum class RendererState
    {
        NOT_READY,
            READY
    } renderer_state_;

    enum class TProxyState
    {
        NOT_READY,
            READY
    } tproxy_state_;

    enum class State
    {
        PREPARING_LOAD,
        LOADING,
        THINKING,
    } state_;

    Timer::UniquePtr think_time_timer_;
};

#endif /* end driver_hpp */
