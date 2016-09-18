#ifndef driver_hpp
#define driver_hpp

#include <event2/event.h>

#include "../../utility/stream_channel.hpp"
#include "../../utility/generic_message_channel.hpp"
#include "../../utility/ipc/generic_ipc_channel.hpp"
#include "../../utility/object.hpp"

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
                    const uint16_t tproxy_ipc_port);

private:

    virtual ~Driver();


    //////////////

    void _on_tproxy_ipc_ch_status(myipc::GenericIpcChannel*,
                                  myipc::GenericIpcChannel::ChannelStatus);

    void _establish_tproxy_tunnel();
    void _on_establish_tunnel_resp(myipc::GenericIpcChannel::RespStatus,
                                   uint16_t len, const uint8_t* buf);

    void _on_tproxy_ipc_msg(myipc::GenericIpcChannel*, uint8_t type,
                            uint16_t len, const uint8_t *data);



    /////

    struct event_base* evbase_;

    myipc::GenericIpcChannel::UniquePtr tproxy_ipc_ch_;

};

#endif /* end driver_hpp */
