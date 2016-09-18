
#include <boost/bind.hpp>

#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"
#include "utility/ipc/transport_proxy/gen/combined_headers"

#include "driver.hpp"


using myipc::GenericIpcChannel;

namespace tproxymsgs = myipc::transport_proxy::messages;
using tproxymsgs::type;

static const uint8_t s_resp_timeout_secs = 5;



#define _LOG_PREFIX(inst) << "driver= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


#undef BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END
#define BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(TYPE, bufbuilder, on_resp_status) \
    auto const __type = tproxymsgs::type_ ## TYPE;                      \
    auto const __resp_type = tproxymsgs::type_ ## TYPE ## Resp;         \
    VLOG(2) << "begin building msg type: " << unsigned(__type);         \
    tproxymsgs::TYPE ## MsgBuilder msgbuilder(bufbuilder);              \
    SCOPE_EXIT {                                                        \
        auto msg = msgbuilder.Finish();                                 \
        bufbuilder.Finish(msg);                                         \
        VLOG(2) << "send msg type: " << unsigned(__type);               \
        tproxy_ipc_ch_->call(                                           \
            __type, bufbuilder.GetSize(),                               \
            bufbuilder.GetBufferPointer(), __resp_type,                 \
            on_resp_status, &s_resp_timeout_secs);                      \
    }

void
Driver::_establish_tproxy_tunnel()
{
    flatbuffers::FlatBufferBuilder bufbuilder;

    BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(
        EstablishTunnel, bufbuilder,
        boost::bind(&Driver::_on_establish_tunnel_resp, this, _2, _3, _4));

    msgbuilder.add_forceReconnect(true);
}

void
Driver::_on_establish_tunnel_resp(GenericIpcChannel::RespStatus status,
                                  uint16_t len, const uint8_t* buf)
{
    if (status == GenericIpcChannel::RespStatus::TIMEDOUT) {
        logself(FATAL) << "establishTunnel command times out";
    }

}

#undef BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END
