
#include <boost/bind.hpp>

#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"
#include "utility/ipc/renderer/gen/combined_headers"

#include "driver.hpp"


using myipc::GenericIpcChannel;

namespace renderermsgs = myipc::renderer::messages;
using renderermsgs::type;

static const uint8_t s_resp_timeout_secs = 5;

const char* Driver::PageLoadStatusStr[] = {
    "OK", "FAILED", "TIMEDOUT"
};


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

    vlogself(1) << "request " << resInstNum << ":" << reqChainIdx;

    ++this_page_load_info_.num_reqs_;

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

    if (!success) {
        ++this_page_load_info_.num_failed_reqs_;
        LOG(WARNING) << "request " << resInstNum << ":" << reqChainIdx
                     << " failed";
    } else {
        ++this_page_load_info_.num_succes_reqs_;
        vlogself(1) << "request " << resInstNum << ":" << reqChainIdx
                    << " finished successfully; new num_succes_reqs_: "
                    << this_page_load_info_.num_succes_reqs_;
    }

    vlogself(2) << "done";
}

void
Driver::_reset_this_page_load_info()
{
    this_page_load_info_.num_failed_reqs_ = 0;
    this_page_load_info_.num_succes_reqs_ = 0;
    this_page_load_info_.num_reqs_ = 0;
    this_page_load_info_.load_start_timepoint_ = 0;
    this_page_load_info_.load_done_timepoint_ = 0;
    this_page_load_info_.model_path_.clear();
}

void
Driver::_report_result(const PageLoadStatus& pageloadstatus,
                       const uint32_t& ttfb_ms)
{
    CHECK_GE(pageloadstatus, 0);
    CHECK_LT(pageloadstatus, (ARRAY_LEN(PageLoadStatusStr)));
    const char* status_str = PageLoadStatusStr[pageloadstatus];

    const auto plt = this_page_load_info_.load_done_timepoint_ - this_page_load_info_.load_start_timepoint_;

    LOG(INFO)
        << "loadnum= " << loadnum_
        << ", vanilla: "
        << status_str
        << ": start= " << this_page_load_info_.load_start_timepoint_
        << " plt= " << (pageloadstatus == PageLoadStatus::OK ? plt : 0)
        << " url= [" << this_page_load_info_.model_path_ << "]"
        << " ttfb= " << (pageloadstatus == PageLoadStatus::OK ? ttfb_ms : 0)
        << " numReqs= " << this_page_load_info_.num_reqs_
        << " numSuccessReqs= " << this_page_load_info_.num_succes_reqs_
        << " numFailedReqs= " << this_page_load_info_.num_failed_reqs_
        ;
}

void
Driver::_renderer_handle_PageLoaded(const myipc::renderer::messages::PageLoadedMsg* msg)
{
    vlogself(2) << "begin";

    CHECK_EQ(state_, State::LOADING_PAGE);

    state_ = State::THINKING;

    _tproxy_stop_defense(false);
    
    // page has loaded

    this_page_load_info_.load_done_timepoint_ = common::gettimeofdayMs();

    _report_result(PageLoadStatus::OK, msg->ttfb_ms());

    // now, start the think time timer
    auto think_time_ms = 120*1000;

    _reset_this_page_load_info();

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

    if (use_tproxy_) {
        _tproxy_maybe_establish_tunnel();
    } else {
        _renderer_load_page();
    }
}

void
Driver::_renderer_load_page()
{
    vlogself(2) << "begin";

    if (use_tproxy_) {
        CHECK_EQ(state_, State::DONE_SET_TPROXY_AUTO_START);
    } else {
        CHECK_EQ(state_, State::DONE_RESET_RENDERER);
    }

    state_ = State::LOADING_PAGE;
    this_page_load_info_.load_start_timepoint_ = common::gettimeofdayMs();
    ++loadnum_;
    if (loadnum_ % 2) {
        this_page_load_info_.model_path_ = "/home/me/cnn_page_model.json";
    } else {
        this_page_load_info_.model_path_ = "/home/me/nytimes_page_model.json";
    }

    {
        flatbuffers::FlatBufferBuilder bufbuilder;
        const auto& model_path = this_page_load_info_.model_path_;
        auto model_fpath = bufbuilder.CreateString(model_path);

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
