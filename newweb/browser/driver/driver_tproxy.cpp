
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
#define BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(TYPE, bufbuilder, on_resp_status, resp_timeout_secs) \
    auto const __type = tproxymsgs::type_ ## TYPE;                      \
    auto const __resp_type = tproxymsgs::type_ ## TYPE ## Resp;         \
    VLOG(2) << "begin building msg type: "                              \
            << tproxymsgs::EnumNametype(__type);                        \
    tproxymsgs::TYPE ## MsgBuilder msgbuilder(bufbuilder);              \
    SCOPE_EXIT {                                                        \
        auto msg = msgbuilder.Finish();                                 \
        bufbuilder.Finish(msg);                                         \
        VLOG(2) << "send msg";                                          \
        static const uint8_t __resp_timeout_secs = (resp_timeout_secs); \
        tproxy_ipc_ch_->call(                                           \
            __type, bufbuilder.GetSize(),                               \
            bufbuilder.GetBufferPointer(), __resp_type,                 \
            on_resp_status, &__resp_timeout_secs);                      \
    }

void
Driver::_tproxy_on_ipc_msg(GenericIpcChannel*, uint8_t,
                           uint16_t, const uint8_t *)
{
    logself(FATAL) << "not reached";
}

void
Driver::_tproxy_maybe_establish_tunnel()
{
    vlogself(2) << "begin";

    if (!tproxy_ipc_ch_ready_) {
        vlogself(2) << "tproxy ipc channel not ready";
        return;
    }
    if ((state_ != State::DONE_RESET_RENDERER)) {
        vlogself(2) << "not yet done resetting renderer";
        return;
    }

    CHECK_EQ(state_, State::DONE_RESET_RENDERER);
    CHECK(tproxy_ipc_ch_ready_);
    state_ = State::ESTABLISH_TPROXY_TUNNEL;

    {
        flatbuffers::FlatBufferBuilder bufbuilder;

        BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(
            EstablishTunnel, bufbuilder,
            boost::bind(&Driver::_tproxy_on_establish_tunnel_resp, this, _2, _3, _4),
            30);
        msgbuilder.add_forceReconnect(true);
    }

    vlogself(2) << "done";
}

void
Driver::_tproxy_on_establish_tunnel_resp(GenericIpcChannel::RespStatus status,
                                  uint16_t len, const uint8_t* buf)
{
    vlogself(2) << "begin";

    CHECK_EQ(state_, State::ESTABLISH_TPROXY_TUNNEL);

    if (status == GenericIpcChannel::RespStatus::TIMEDOUT) {
        logself(FATAL) << "timed out establishing tunnel";
    }

    auto msg = tproxymsgs::GetEstablishTunnelRespMsg(buf);
    CHECK(msg->tunnelIsReady());

    logself(INFO) << "CSP allRecvByteCountSoFar: " << msg->allRecvByteCountSoFar()
                  << " usefulRecvByteCountSoFar: " << msg->usefulRecvByteCountSoFar();

    state_ = State::DONE_ESTABLISH_TPROXY_TUNNEL;

    _tproxy_set_auto_start_defense_on_next_send();

    vlogself(2) << "done";
}

void
Driver::_tproxy_set_auto_start_defense_on_next_send()
{
    vlogself(2) << "begin";

    CHECK(tproxy_ipc_ch_ready_);

    CHECK_EQ(state_, State::DONE_ESTABLISH_TPROXY_TUNNEL);
    state_ = State::SET_TPROXY_AUTO_START;

    {
        flatbuffers::FlatBufferBuilder bufbuilder;

        BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(
            SetAutoStartDefenseOnNextSend, bufbuilder,
            boost::bind(&Driver::_tproxy_on_set_auto_start_defense_on_next_send_resp,
                        this, _2, _3, _4),
            3);
    }

    vlogself(2) << "done";
}

void
Driver::_tproxy_on_set_auto_start_defense_on_next_send_resp(
    GenericIpcChannel::RespStatus status,
    uint16_t, const uint8_t* buf)
{
    vlogself(2) << "begin";

    CHECK_EQ(state_, State::SET_TPROXY_AUTO_START);

    if (status == GenericIpcChannel::RespStatus::TIMEDOUT) {
        logself(FATAL) << "command timed out";
    }

    auto msg = tproxymsgs::GetSetAutoStartDefenseOnNextSendRespMsg(buf);
    CHECK(msg->ok());

    vlogself(2) << "tproxy is ready";

    state_ = State::DONE_SET_TPROXY_AUTO_START;

    if (loadnum_) {
        // we got here after a page load, so we need to think
        _start_thinking();
    } else {
        // start loading immediately since we got here from
        // initializing
        _renderer_load_page();
    }

    vlogself(2) << "done";
}

void
Driver::_tproxy_stop_defense(const bool& right_now)
{
    vlogself(2) << "begin";

    CHECK(tproxy_ipc_ch_ready_);

    // CHECK_EQ(state_, State::GRACE_PERIOD_AFTER_DOM_LOAD_EVENT);

    {
        flatbuffers::FlatBufferBuilder bufbuilder;

        BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(
            StopDefense, bufbuilder,
            boost::bind(&Driver::_tproxy_on_stop_defense_resp,
                        this, _2, _3, _4),
            3);
    }

    vlogself(2) << "done";
}

void
Driver::_tproxy_on_stop_defense_resp(
    GenericIpcChannel::RespStatus status,
    uint16_t, const uint8_t* buf)
{
    vlogself(2) << "begin";

    // CHECK_EQ(state_, State::GRACE_PERIOD_AFTER_DOM_LOAD_EVENT);

    if (status == GenericIpcChannel::RespStatus::TIMEDOUT) {
        logself(FATAL) << "command timed out";
    }

    vlogself(2) << "done";
}
