
#include <boost/bind.hpp>

#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"
#include "utility/ipc/renderer/gen/combined_headers"

#include "driver.hpp"


using myipc::GenericIpcChannel;

namespace renderermsgs = myipc::renderer::messages;
using renderermsgs::type;

static const uint8_t s_resp_timeout_secs = 5;



#define _LOG_PREFIX(inst) 

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


#define IPC_MSG_HANDLER(TYPE)                                           \
    case renderermsgs::type_ ## TYPE: {                                 \
        _renderer_handle_ ## TYPE(renderermsgs::Get ## TYPE ## Msg(data)); \
    }                                                                   \
    break;


void
Driver::_renderer_on_ipc_msg(GenericIpcChannel*, uint8_t type,
                             uint16_t, const uint8_t *data)
{
    vlogself(2) << "type: " << renderermsgs::EnumNametype((renderermsgs::type)type);

    switch (type) {

        IPC_MSG_HANDLER(RequestWillBeSent)
        IPC_MSG_HANDLER(RequestFinished)
        IPC_MSG_HANDLER(PageLoaded)

    default:
        logself(FATAL) << "invalid IPC message type " << unsigned(type);
        break;
    }
}

void
Driver::_renderer_handle_RequestWillBeSent(
    const myipc::renderer::messages::RequestWillBeSentMsg* msg)
{
    vlogself(2) << "begin";

    // the page might have fired the "load" event already
    // CHECK_EQ(state_, State::LOADING_PAGE);

    const auto resInstNum = msg->resInstNum();
    const auto reqChainIdx = msg->reqChainIdx();

    CHECK_GT(resInstNum, 0);
    CHECK_GE(reqChainIdx, 0);

    LOG(INFO) << "request " << resInstNum << ":" << reqChainIdx;

    vlogself(2) << "done";
}

void
Driver::_renderer_handle_RequestFinished(
    const myipc::renderer::messages::RequestFinishedMsg* msg)
{
    vlogself(2) << "begin";

    // the page might have fired the "load" event already
    // CHECK_EQ(state_, State::LOADING_PAGE);

    const auto resInstNum = msg->resInstNum();
    const auto reqChainIdx = msg->reqChainIdx();
    const auto success = msg->success();

    CHECK_GT(resInstNum, 0);
    CHECK_GE(reqChainIdx, 0);

    LOG(INFO) << "request " << resInstNum << ":" << reqChainIdx
              << " finished, success= " << success;

    vlogself(2) << "done";
}

void
Driver::_renderer_handle_PageLoaded(const myipc::renderer::messages::PageLoadedMsg* msg)
{
    vlogself(2) << "begin";

    CHECK_EQ(state_, State::LOADING_PAGE);

    // page has loaded

    LOG(INFO) << "page loaded. TTFB= " << msg->ttfb_ms() << " ms";

    // TODO: inspect the success/failure bool, and do logging

    // now, start the think time timer
    auto think_time_ms = 120*1000;

    state_ = State::THINKING;

    think_time_timer_->start(think_time_ms);

    vlogself(2) << "done";
}

void
Driver::_renderer_reset()
{
    vlogself(2) << "begin";

    CHECK((state_ == State::INITIAL)
          || (state_ == State::THINKING));
    state_ = State::RESET_RENDERER;

    {
        flatbuffers::FlatBufferBuilder bufbuilder;
        BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(
            Reset, bufbuilder,
            boost::bind(&Driver::_renderer_on_reset_resp, this, _2, _3, _4));
    }

    vlogself(2) << "done";
}

void
Driver::_renderer_on_reset_resp(GenericIpcChannel::RespStatus status,
                                uint16_t len, const uint8_t* buf)
{
    CHECK_EQ(state_, State::RESET_RENDERER);

    if (status == GenericIpcChannel::RespStatus::TIMEDOUT) {
        logself(FATAL) << "Load command times out";
    }

    state_ = State::DONE_RESET_RENDERER;

    _tproxy_establish_tunnel();
}

void
Driver::_renderer_load_page()
{
    vlogself(2) << "begin";

    CHECK_EQ(state_, State::DONE_SET_TPROXY_AUTO_START);
    state_ = State::LOADING_PAGE;

    {
        flatbuffers::FlatBufferBuilder bufbuilder;
        auto model_fpath = bufbuilder.CreateString("/home/me/page_model.json");

        BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(
            LoadPage, bufbuilder,
            boost::bind(&Driver::_renderer_on_load_page_resp, this, _2, _3, _4));

        msgbuilder.add_model_fpath(model_fpath);
    }

    vlogself(2) << "done";
}

void
Driver::_renderer_on_load_page_resp(GenericIpcChannel::RespStatus status,
                      uint16_t len, const uint8_t* buf)
{
    CHECK_EQ(state_, State::LOADING_PAGE);
    if (status == GenericIpcChannel::RespStatus::TIMEDOUT) {
        logself(FATAL) << "Load command times out";
    }
}

#undef BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END
