
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "easylogging++.h"
#include "json_stream_channel.hpp"


namespace myio
{


JSONStreamChannel::JSONStreamChannel(
    StreamChannel::UniquePtr channel, JSONStreamChannelObserver* observer)
    : channel_(std::move(channel)), observer_(observer)
    , state_(StreamState::READ_TYPE_AND_LENGTH)
    , msg_type_(0), msg_len_(0)
{
    // set myself as observer of the underlying channel
    channel_->set_observer(this);
}

void
JSONStreamChannel::onNewReadDataAvailable(StreamChannel* channel) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    _consume_input();
}

void
JSONStreamChannel::_consume_input()
{
    auto num_avail_bytes = channel_->get_avail_input_length();
    auto done = false;

    do {
        // loop to process all complete msgs
        switch (state_) {
        case StreamState::READ_TYPE_AND_LENGTH:
            VLOG(2) << "trying to read msg type and length";
            CHECK_EQ(msg_len_, 0);
            if (num_avail_bytes >= (MSG_TYPE_SIZE + MSG_LEN_SIZE)) {
                auto rv = channel_->read((uint8_t*)&msg_type_, MSG_TYPE_SIZE);
                CHECK_EQ(rv, MSG_TYPE_SIZE);
                num_avail_bytes -= rv;
                // convert to host byte order
                msg_type_ = ntohs(msg_type_);

                rv = channel_->read((uint8_t*)&msg_len_, MSG_LEN_SIZE);
                CHECK_EQ(rv, MSG_LEN_SIZE);
                num_avail_bytes -= rv;
                // convert to host byte order
                msg_len_ = ntohs(msg_len_);

                // update state
                state_ = StreamState::READ_MSG;
                VLOG(2) << "got type= " << msg_type_ << " and len= " << msg_len_;
            } else {
                VLOG(2) << "not enough bytes yet";
                done = true; // to break out of loop
            }
            break;
        case StreamState::READ_MSG:
            VLOG(2) << "trying to read msg of length " << msg_len_;
            if (num_avail_bytes >= msg_len_) {
                DestructorGuard db(this);
                rapidjson::Document msg;

                if (msg_len_) {
                    uint8_t* buf = channel_->peek(msg_len_);
                    CHECK_NOTNULL(buf);
                    msg.Parse((const char*)buf, msg_len_);
                    channel_->drain(msg_len_);
                }

                // update state
                state_ = StreamState::READ_TYPE_AND_LENGTH;

                // notify
                observer_->onRecvMsg(this, msg_type_, msg);
            } else {
                VLOG(2) << "not enough bytes yet";
                done = true; // to break out of loop
            }
            break;
        default:
            CHECK(false); // not reached
        }
    } while (!done);

    // update read low watermark
    _update_read_watermark();
}

void
JSONStreamChannel::sendMsg(uint16_t type, const rapidjson::Document& msg)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    msg.Accept(writer);

    CHECK_LT(sb.GetSize(), 0x3fff); // we realy shouldn't need
                                     // 16k-byte json msgs
    uint16_t len = sb.GetSize();

    _send_type_and_len(type, len);

    // make sure to write the right amount, i.e., "len" has already
    // been converted to network-byte order
    const auto rv = channel_->write((const uint8_t*)sb.GetString(), sb.GetSize());
    CHECK_EQ(rv, 0);
}

void
JSONStreamChannel::sendMsg(uint16_t type)
{
    _send_type_and_len(type, 0);
}

/* type and len values should be HOST byte order */
void
JSONStreamChannel::_send_type_and_len(uint16_t type, uint16_t len)
{
    type = htons(type);
    auto rv = channel_->write((const uint8_t*)&type, sizeof(type));
    CHECK_EQ(rv, 0);

    len = htons(len);
    rv = channel_->write((const uint8_t*)&len, sizeof(len));
    CHECK_EQ(rv, 0);
}

void
JSONStreamChannel::_update_read_watermark()
{
    if (state_ == StreamState::READ_TYPE_AND_LENGTH) {
        CHECK_EQ(msg_len_, 0);
        channel_->set_read_watermark(MSG_TYPE_SIZE + MSG_LEN_SIZE, 0);
    } else {
        CHECK_GT(msg_len_, 0);
        channel_->set_read_watermark(msg_len_, 0);
    }
}

void
JSONStreamChannel::onEOF(StreamChannel* channel) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    state_ = StreamState::CLOSED;
    DestructorGuard db(this);
    VLOG(2) << ("json stream notified of transport close");
    observer_->onEOF(this);
}

void
JSONStreamChannel::onError(StreamChannel* channel, int errorcode) noexcept
{
    CHECK_EQ(channel_.get(), channel);
    state_ = StreamState::CLOSED;
    VLOG(2) << ("json stream notified of transport error");
    DestructorGuard db(this);
    observer_->onError(this, errorcode);
}

JSONStreamChannel::~JSONStreamChannel()
{
}


}
