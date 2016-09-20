
#include <boost/bind.hpp>

#include "../easylogging++.h"
#include "../common.hpp"
#include "generic_ipc_channel.hpp"


using myio::GenericMessageChannel;
using myio::StreamChannel;


#define _LOG_PREFIX(inst) << "genIpcCh= " << (inst)->objId() << ": "

/* "inst" stands for instance, as in, instance of a class */
#define vloginst(level, inst) VLOG(level) _LOG_PREFIX(inst)
#define vlogself(level) vloginst(level, this)

#define dvloginst(level, inst) DVLOG(level) _LOG_PREFIX(inst)
#define dvlogself(level) dvloginst(level, this)

#define loginst(level, inst) LOG(level) _LOG_PREFIX(inst)
#define logself(level) loginst(level, this)


namespace myipc
{

GenericIpcChannel::GenericIpcChannel(struct event_base* evbase,
                                     StreamChannel::UniquePtr stream_ch,
                                     OnMsgCb msg_cb, ChannelStatusCb channel_status_cb)
    : evbase_(evbase)
    , stream_ch_(std::move(stream_ch))
    , is_client_(true)
    , msg_cb_(msg_cb)
    , channel_status_cb_(channel_status_cb)
    , next_call_msg_id_(1) /* client use odd call ids */
{
    stream_ch_->start_connecting(this);
}

GenericIpcChannel::GenericIpcChannel(struct event_base* evbase,
                                     StreamChannel::UniquePtr stream_ch,
                                     OnMsgCb msg_cb, CalledCb called_cb,
                                     ChannelStatusCb channel_status_cb)
    : evbase_(evbase)
    , stream_ch_(std::move(stream_ch))
    , is_client_(false)
    , msg_cb_(msg_cb)
    , called_cb_(called_cb)
    , channel_status_cb_(channel_status_cb)
    , next_call_msg_id_(2) /* server use even call ids */
{
    gen_msg_ch_.reset(
        new GenericMessageChannel(std::move(stream_ch_), this));
}

void
GenericIpcChannel::sendMsg(uint8_t type, uint16_t len, const uint8_t* buf)
{
    DCHECK(gen_msg_ch_) << "generic ipc ch not avail yet";
    gen_msg_ch_->sendMsg(type, len, buf);
}

void
GenericIpcChannel::sendMsg(uint8_t type)
{
    DCHECK(gen_msg_ch_) << "generic ipc ch not avail yet";
    gen_msg_ch_->sendMsg(type);
}

void
GenericIpcChannel::call(uint8_t type, uint16_t len, const uint8_t* buf,
                        uint8_t resp_type, OnRespStatusCb on_resp_status_cb,
                        const uint8_t *timeoutSecs)
{
    DCHECK(gen_msg_ch_) << "generic ipc ch not avail yet";
    CHECK(is_client_);

    vlogself(2) << "sending a call type " << unsigned(type)
                << " timeoutSecs: " << (timeoutSecs ? *timeoutSecs : 0);

    auto const id = next_call_msg_id_;
    next_call_msg_id_ += 2;

    pending_calls_[id].call_msg_type = type;
    pending_calls_[id].exp_resp_msg_type = resp_type;
    pending_calls_[id].on_resp_status_cb = on_resp_status_cb;
    if (timeoutSecs) {
        CHECK_LE(*timeoutSecs, 5);
        struct timeval timeout_tv = {0};
        timeout_tv.tv_sec = *timeoutSecs;

        pending_calls_[id].timeout_timer.reset(
            new Timer(evbase_, true,
                      boost::bind(&GenericIpcChannel::_on_timeout_waiting_resp,
                                  this, _1, id)));
        pending_calls_[id].timeout_timer->start(&timeout_tv);
    }

    gen_msg_ch_->sendMsg(type, len, buf, id);
}

void
GenericIpcChannel::reply(uint32_t id, uint8_t type, uint16_t len,
                         const uint8_t* buf)
{
    vlogself(2) << "sending reply id: " << id << " type: " << unsigned(type);
    gen_msg_ch_->sendMsg(type, len, buf, id);
}

void
GenericIpcChannel::onConnected(StreamChannel*) noexcept
{
    gen_msg_ch_.reset(
        new GenericMessageChannel(std::move(stream_ch_), this));
    DestructorGuard dg(this);
    channel_status_cb_(this, ChannelStatus::READY);
}

void
GenericIpcChannel::onConnectError(StreamChannel*, int) noexcept
{
    LOG(FATAL) << "trouble!";
}

void
GenericIpcChannel::onConnectTimeout(StreamChannel*) noexcept
{
    LOG(FATAL) << "not reached!";
}

void
GenericIpcChannel::onRecvMsg(GenericMessageChannel*, uint8_t type, uint32_t id,
                            uint16_t len, const uint8_t* buf) noexcept
{
    DestructorGuard dg(this);

    if (id == 0) {
        msg_cb_(this, type, len, buf);
    } else {
        if (inMap(pending_calls_, id)) {
            // it's a response to one of our pending call
            CHECK_EQ(type, pending_calls_[id].exp_resp_msg_type)
                << "call id " << id
                << " type " << unsigned(pending_calls_[id].call_msg_type)
                << " expects resp type " << unsigned(pending_calls_[id].exp_resp_msg_type)
                << " but got type " << unsigned(type);
            pending_calls_[id].on_resp_status_cb(this, RespStatus::RECV, len, buf);
            pending_calls_.erase(id);
        } else {
            // receiving a call, we must be server
            CHECK(!is_client_) << "currently only clients can send calls to servers";
            called_cb_(this, id, type, len, buf);
        }
    }
}

void
GenericIpcChannel::onEOF(myio::GenericMessageChannel*) noexcept
{
    DestructorGuard dg(this);
    channel_status_cb_(this, ChannelStatus::CLOSED);
}

void
GenericIpcChannel::onError(myio::GenericMessageChannel*, int) noexcept
{
    DestructorGuard dg(this);
    channel_status_cb_(this, ChannelStatus::CLOSED);
}

void
GenericIpcChannel::_on_timeout_waiting_resp(Timer*, uint32_t id)
{
    CHECK(inMap(pending_calls_, id));

    pending_calls_[id].on_resp_status_cb(
        this, RespStatus::TIMEDOUT, 0, nullptr);
    pending_calls_.erase(id);
}

} // end namespace myipc
