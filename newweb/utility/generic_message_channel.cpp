
#include <event2/buffer.h>

#include "easylogging++.h"
#include "generic_message_channel.hpp"

namespace myio
{


GenericMessageChannel::GenericMessageChannel(
    StreamChannel::UniquePtr channel, GenericMessageChannelObserver* observer)
    : channel_(std::move(channel)), observer_(observer)
    , state_(StreamState::READ_TYPE_AND_LENGTH)
    , msg_type_(0), msg_len_(0)
{
    // set myself as observer of the underlying channel
    channel_->set_observer(this);
}

void
GenericMessageChannel::onNewReadDataAvailable(StreamChannel* channel) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    _consume_input();
}

void
GenericMessageChannel::_consume_input()
{
    auto keep_consuming = true;
    auto input_evb = channel_->get_input_evbuf();

    // only drain buffer after we read full msg, to reduce memory
    // operations

    do {
        const auto num_avail_bytes = evbuffer_get_length(input_evb);
        auto total_len = 0;
        // loop to process all complete msgs
        switch (state_) {
        case StreamState::READ_TYPE_AND_LENGTH:
            VLOG(2) << "trying to read msg type and length";
            CHECK_EQ(msg_len_, 0);
            if (num_avail_bytes >= HEADER_SIZE) {
                // we're using a libevent version without
                // copyout[_from]()
                auto buf = evbuffer_pullup(input_evb, HEADER_SIZE);
                CHECK_NOTNULL(buf);

                memcpy((uint8_t*)&msg_type_, buf, MSG_TYPE_SIZE);
                memcpy((uint8_t*)&msg_len_, buf + MSG_TYPE_SIZE, MSG_LEN_SIZE);

                // convert to host byte order
                msg_type_ = ntohs(msg_type_);
                msg_len_ = ntohs(msg_len_);

                // update state
                state_ = StreamState::READ_MSG;
                VLOG(2) << "got type= " << msg_type_ << " and len= " << msg_len_;

                // not draining input buf here!
            } else {
                VLOG(2) << "not enough bytes yet";
                keep_consuming = false; // to break out of loop
            }
            break;
        case StreamState::READ_MSG:
            VLOG(2) << "trying to read msg of length " << msg_len_;
            total_len = (HEADER_SIZE + msg_len_);
            if (num_avail_bytes >= total_len) {
                DestructorGuard db(this);
                auto data = evbuffer_pullup(input_evb, total_len);
                CHECK_NOTNULL(data);

                // notify
                observer_->onRecvMsg(
                    this, msg_type_, msg_len_, data + HEADER_SIZE);

                // update state
                state_ = StreamState::READ_TYPE_AND_LENGTH;
                msg_len_ = 0;

                auto rv = evbuffer_drain(input_evb, total_len);
                CHECK_EQ(rv, 0);
            } else {
                VLOG(2) << "not enough bytes yet";
                keep_consuming = false; // to break out of loop
            }
            break;
        default:
            CHECK(false); // not reached
        }
    } while (keep_consuming);

    // update read low watermark
    _update_read_watermark();
}

void
GenericMessageChannel::sendMsg(uint16_t type, uint16_t len, const uint8_t* data)
{
    VLOG(2) << "sending msg type: " << type << ", len: " << len;
    _send_type_and_len(type, len);

    if (len) {
        const auto rv = channel_->write(data, len);
        CHECK_EQ(rv, 0);
    }
}

void
GenericMessageChannel::sendMsg(uint16_t type)
{
    _send_type_and_len(type, 0);
}

/* type and len values should be HOST byte order */
void
GenericMessageChannel::_send_type_and_len(uint16_t type, uint16_t len)
{
    type = htons(type);
    auto rv = channel_->write((const uint8_t*)&type, sizeof(type));
    CHECK_EQ(rv, 0);

    len = htons(len);
    rv = channel_->write((const uint8_t*)&len, sizeof(len));
    CHECK_EQ(rv, 0);
}

void
GenericMessageChannel::_update_read_watermark()
{
    if (state_ == StreamState::READ_TYPE_AND_LENGTH) {
        CHECK_EQ(msg_len_, 0);
        channel_->set_read_watermark(HEADER_SIZE, 0);
    } else {
        CHECK_GT(msg_len_, 0);
        channel_->set_read_watermark(HEADER_SIZE + msg_len_, 0);
    }
}

void
GenericMessageChannel::onEOF(StreamChannel* channel) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    state_ = StreamState::CLOSED;
    DestructorGuard db(this);
    VLOG(2) << "generic msg stream notified of transport close";
    observer_->onEOF(this);
}

void
GenericMessageChannel::onError(StreamChannel* channel, int errorcode) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    state_ = StreamState::CLOSED;
    VLOG(2) << "generic msg stream notified of transport error";
    DestructorGuard db(this);
    observer_->onError(this, errorcode);
}


}
