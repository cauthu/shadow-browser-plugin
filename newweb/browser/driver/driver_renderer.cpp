
#include <boost/bind.hpp>

#include "../../utility/common.hpp"
#include "../../utility/easylogging++.h"
#include "../../utility/folly/ScopeGuard.h"
#include "utility/ipc/renderer/gen/combined_headers"

#include "driver.hpp"


using myipc::GenericIpcChannel;

namespace renderermsgs = myipc::renderer::messages;
using renderermsgs::type;

static const uint8_t s_ipc_cmd_resp_timeout_secs = 5;

static const auto wait_for_more_requests_ms = 2*1000;


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
            on_resp_status, &s_ipc_cmd_resp_timeout_secs);              \
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
        IPC_MSG_HANDLER(PageLoadFailed)

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

    if (this_page_load_info_.DOM_load_event_fired_timepoint_ > 0) {
        ++this_page_load_info_.num_after_DOM_load_event_reqs_;
    }

    vlogself(2) << "cancel wait_for_more_requests_timer_";
    wait_for_more_requests_timer_->cancel();

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

    if (this_page_load_info_.DOM_load_event_fired_timepoint_ > 0) {
        wait_for_more_requests_timer_->cancel();
        wait_for_more_requests_timer_->start(wait_for_more_requests_ms);
    }

    vlogself(2) << "done";
}

void
Driver::_reset_this_page_load_info()
{
    this_page_load_info_.num_failed_reqs_ = 0;
    this_page_load_info_.num_succes_reqs_ = 0;
    this_page_load_info_.num_reqs_ = 0;
    this_page_load_info_.num_after_DOM_load_event_reqs_ = 0;
    this_page_load_info_.load_start_timepoint_ = 0;
    this_page_load_info_.DOM_load_event_fired_timepoint_ = 0;
    this_page_load_info_.page_model_idx_ = 0;

    this_page_load_info_.page_load_status_ = PageLoadStatus::NONE;
    this_page_load_info_.ttfb_ms_ = 0;
}

void
Driver::_report_result()
{
    auto& tpli = this_page_load_info_;
    const auto& pageloadstatus = tpli.page_load_status_;
    const auto& ttfb_ms = tpli.ttfb_ms_;

    CHECK((pageloadstatus != PageLoadStatus::NONE)
	  && (pageloadstatus != PageLoadStatus::PENDING));

    const char* status_str = s_page_load_status_to_string(pageloadstatus);
    CHECK_NOTNULL(status_str);

    const auto plt = tpli.DOM_load_event_fired_timepoint_ - tpli.load_start_timepoint_;

    LOG(INFO)
        << "loadnum= " << loadnum_
        << ", webmode= vanilla"
        << ", proxyMode= " << browser_proxy_mode_
        << ": loadResult= " << status_str
        << ": startSec= " << (tpli.load_start_timepoint_ / 1000)
        << " plt= " << (pageloadstatus == PageLoadStatus::OK ? plt : 0)
        << " page= [" << page_models_[tpli.page_model_idx_].first << "]"
        << " ttfb= " << (pageloadstatus == PageLoadStatus::OK ? ttfb_ms : 0)
        << " numReqs= " << tpli.num_reqs_
        << " numSuccess= " << tpli.num_succes_reqs_
        << " numFailed= " << tpli.num_failed_reqs_
        << " numAfterDOMLoadEvent= " << tpli.num_after_DOM_load_event_reqs_
        ;
}

void
Driver::_renderer_handle_PageLoaded(const myipc::renderer::messages::PageLoadedMsg* msg)
{
    vlogself(2) << "begin";

    CHECK_EQ(msg->load_id(), loadnum_)
        << "expect " << loadnum_ << " got " << msg->load_id();

    if (state_ != State::LOADING_PAGE) {
        // we're not loading, so just ignore; this can happen when we
        // time out a page load here, and before we can tell renderer
        // to stop the page load, it already sent us this msg
        return;
    }

    logself(INFO) << "DOM \"load\" event has fired; start waiting for more requests";

    state_ = State::WAIT_FOR_MORE_REQUESTS_AFTER_DOM_LOAD_EVENT;

    page_load_timeout_timer_->cancel();

    if (using_tproxy_) {
        _tproxy_stop_defense(false);
    }
    
    // page "load" event fired

    this_page_load_info_.DOM_load_event_fired_timepoint_ = common::gettimeofdayMs();
    this_page_load_info_.page_load_status_ = PageLoadStatus::OK;
    this_page_load_info_.ttfb_ms_ = msg->ttfb_ms();

    wait_for_more_requests_timer_->start(wait_for_more_requests_ms);

    vlogself(2) << "done";
}

void
Driver::_renderer_handle_PageLoadFailed(const myipc::renderer::messages::PageLoadFailedMsg* msg)
{
    vlogself(2) << "begin";

    CHECK_EQ(msg->load_id(), loadnum_)
        << "expect " << loadnum_ << " got " << msg->load_id();

    if (state_ != State::LOADING_PAGE) {
        // we're not loading, so just ignore; this can happen when we
        // time out a page load here, and before we can tell renderer
        // to stop the page load, it already sent us this msg
        return;
    }

    logself(WARNING) << "page load has failed";

    CHECK_EQ(state_, State::LOADING_PAGE);

    page_load_timeout_timer_->cancel();

    auto& tpli = this_page_load_info_;
    CHECK_EQ(tpli.page_load_status_, PageLoadStatus::PENDING);
    tpli.page_load_status_ = PageLoadStatus::FAILED;

    _report_result();

#ifndef IN_SHADOW

    // running outside shadow, so exit after one page load
    CHECK(0) << "need testing";
    CHECK_EQ(loadnum_, 1);
    logself(INFO) << "exiting";

#endif

    _reset_this_page_load_info();

    _renderer_reset();

    if (using_tproxy_) {
        _tproxy_stop_defense(false);
    }

    vlogself(2) << "done";
}

void
Driver::_renderer_reset()
{
    vlogself(2) << "begin";

    // CHECK((state_ == State::INITIAL)
    //       || (state_ == State::LOADING_PAGE)
    //       || (state_ == State::LOADING_PAGE));
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
        logself(FATAL) << "reset command times out";
    }

    state_ = State::DONE_RESET_RENDERER;

    if (using_tproxy_) {
        _tproxy_maybe_establish_tunnel();
    } else {
        if (loadnum_) {
            // we got here after a page load, so we need to think
            _start_thinking();
        } else {
            // start loading immediately since we got here from
            // initializing
            _renderer_load_page();
        }
    }
}

void
Driver::_start_thinking()
{
    CHECK((state_ == State::DONE_RESET_RENDERER)
          || (state_ == State::DONE_SET_TPROXY_AUTO_START));
    state_ = State::THINKING;

    const auto think_time_ms = (*think_time_rand_gen_)();
    logself(INFO) << "start thinking for " << think_time_ms << " ms";
    think_time_timer_->start(think_time_ms);
}

void
Driver::_renderer_load_page()
{
    vlogself(2) << "begin";

    if (using_tproxy_) {
        CHECK((state_ == State::THINKING)
              || (state_ == State::DONE_SET_TPROXY_AUTO_START))
            << "unexpected state " << common::as_integer(state_);
    } else {
        /* normally we get here after thinking, except for the very
         * first load, for which there was no thinking before it
         */
        CHECK((state_ == State::THINKING)
              || (state_ == State::DONE_RESET_RENDERER))
            << "unexpected state " << common::as_integer(state_);
    }

    state_ = State::LOADING_PAGE;

    _reset_this_page_load_info();

    auto& tpli = this_page_load_info_;
    tpli.load_start_timepoint_ = common::gettimeofdayMs();
    tpli.page_load_status_ = PageLoadStatus::PENDING;
    ++loadnum_;

    tpli.page_model_idx_ = (*page_model_rand_idx_gen_)();
    CHECK_GE(tpli.page_model_idx_, 0);
    CHECK_LT(tpli.page_model_idx_, page_models_.size());

    wait_for_more_requests_timer_.cancel();

    vlogself(1) << "picked new page_model_idx_= " << tpli.page_model_idx_;

    logself(INFO) << "start loading page [" << page_models_[tpli.page_model_idx_].first << "]";

    {
        flatbuffers::FlatBufferBuilder bufbuilder;
        const auto& model_path = page_models_[tpli.page_model_idx_].second;
        auto model_fpath = bufbuilder.CreateString(model_path);

        BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END(
            LoadPage, bufbuilder,
            boost::bind(&Driver::_renderer_on_load_page_resp, this, _2, _3, _4));

        msgbuilder.add_model_fpath(model_fpath);
        msgbuilder.add_load_id(loadnum_);
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

    // 120 seconds
    static const uint32_t page_load_timeout_ms = 120*1000;
    page_load_timeout_timer_->start(page_load_timeout_ms);
}

const char*
Driver::s_page_load_status_to_string(const PageLoadStatus& status)
{
    switch (status) {
    case PageLoadStatus::PENDING:
        return "PENDING";
        break;
    case PageLoadStatus::OK:
        return "OK";
        break;
    case PageLoadStatus::FAILED:
        return "FAILED";
        break;
    case PageLoadStatus::TIMEDOUT:
        return "TIMEDOUT";
        break;
    default:
        LOG(FATAL) << "not reached";
        break;
    }
    return nullptr;
}

#undef BEGIN_BUILD_CALL_MSG_AND_SEND_AT_END
