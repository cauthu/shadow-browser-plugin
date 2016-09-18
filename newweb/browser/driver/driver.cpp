
#include <boost/bind.hpp>

#include "../../utility/tcp_channel.hpp"
#include "../../utility/common.hpp"
#include "utility/ipc/transport_proxy/gen/combined_headers"

#include "driver.hpp"


using myio::TCPChannel;
using myipc::GenericIpcChannel;


#define _LOG_PREFIX(inst) << "driver= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)



Driver::Driver(struct event_base* evbase,
               const uint16_t tproxy_ipc_port)
    : evbase_(evbase)
{
    vlogself(2) << "connect to tproxy ipc port: " << tproxy_ipc_port;
    myio::TCPChannel::UniquePtr tcpch1(
        new myio::TCPChannel(evbase_, common::getaddr("localhost"),
                             tproxy_ipc_port, nullptr));
    tproxy_ipc_ch_.reset(
        new GenericIpcChannel(
            evbase_,
            std::move(tcpch1),
            boost::bind(&Driver::_on_tproxy_ipc_msg, this, _1, _2, _3, _4),
            boost::bind(&Driver::_on_tproxy_ipc_ch_status, this, _1, _2)));
}

Driver::~Driver()
{
}

void
Driver::_on_tproxy_ipc_msg(GenericIpcChannel*, uint8_t,
                           uint16_t, const uint8_t *)
{
    logself(FATAL) << "not reached";
}

void
Driver::_on_tproxy_ipc_ch_status(GenericIpcChannel*,
                                 GenericIpcChannel::ChannelStatus status)
{
    switch (status) {
    case GenericIpcChannel::ChannelStatus::READY: {
        _establish_tproxy_tunnel();
        break;
    }

    case GenericIpcChannel::ChannelStatus::CLOSED: {
        logself(FATAL) << "to do";
        break;
    }

    default:
        logself(FATAL) << "not reached";
        break;
    }
}
