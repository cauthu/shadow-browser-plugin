
#include <boost/bind.hpp>

#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"
#include "utility/ipc/renderer/gen/combined_headers"

#include "driver.hpp"


using myipc::GenericIpcChannel;

namespace renderermsgs = myipc::renderer::messages;
using renderermsgs::type;

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
    auto const __type = renderermsgs::type_ ## TYPE;                    \
    auto const __resp_type = renderermsgs::type_ ## TYPE ## Resp;       \
    VLOG(2) << "begin building msg type: " << unsigned(__type);         \
    renderermsgs::TYPE ## MsgBuilder msgbuilder(bufbuilder);            \
    SCOPE_EXIT {                                                        \
        auto msg = msgbuilder.Finish();                                 \
        bufbuilder.Finish(msg);                                         \
        VLOG(2) << "send msg type: " << unsigned(__type);               \
        renderer_ipc_ch_->call(                                         \
            __type, bufbuilder.GetSize(),                               \
            bufbuilder.GetBufferPointer(), __resp_type,                 \
            on_resp_status, &s_resp_timeout_secs);                      \
    }

void
Driver::_on_renderer_ipc_msg(GenericIpcChannel*, uint8_t,
                             uint16_t, const uint8_t *)
{
    logself(FATAL) << "to do";
}


void
Driver::_maybe_start_load()
{
    vlogself(2) << "begin";
    if (renderer_state_ == RendererState::READY) {
        _load();
    }
    vlogself(2) << "done";
}

void
Driver::_load()
{
    flatbuffers::FlatBufferBuilder bufbuilder;

    BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(
        Load, bufbuilder,
        boost::bind(&Driver::_on_load_resp, this, _2, _3, _4));
}

void
Driver::_on_load_resp(GenericIpcChannel::RespStatus status,
                      uint16_t len, const uint8_t* buf)
{
    if (status == GenericIpcChannel::RespStatus::TIMEDOUT) {
        logself(FATAL) << "establishTunnel command times out";
    }

    // CHECK_EQ(status, GenericIpcChannel::RespStatus::RECV);

    // auto msg = msgs::Get
    // renderer_state_ = RendererState::READY;

    // _maybe_start_load();
}

#undef BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END
