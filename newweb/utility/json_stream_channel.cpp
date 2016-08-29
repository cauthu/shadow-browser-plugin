
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "myassert.h"
#include "json_stream_channel.hpp"


namespace myio
{

// IPCClientChannel::IPCClientChannel(
//     struct event_base* evbase,
//     const in_addr_t& addr, const in_port_t& port,
//     IPCChannelObserver* observer)
//     : msg_proto_state_(MSGProtoState::READ_LENGTH)
// {
//     tcp_channel_.reset(new TCPClientChannel(evbase, addr, port, this));
// }

// bool
// IPCClientChannel::start_connecting()
// {
//     return tcp_channel_->start_connecting(this);
// }


// void
// IPCClientChannel::onConnected(TCPChannel* tcp_channel) noexcept
// {
//     myassert(tcp_channel_.get() == tcp_channel);
//     _update_read_watermark();
// }

// void
// IPCClientChannel::onConnectError(TCPChannel* tcp_channel, int errorcode) noexcept
// {
//     myassert(tcp_channel_.get() == tcp_channel);
// }

// void
// IPCClientChannel::onNewReadDataAvailable(TCPChannel* tcp_channel) noexcept
// {
//     myassert(tcp_channel_.get() == tcp_channel);
//     _consume_input(tcp_channel);
// }

// void
// IPCClientChannel::_consume_input(TCPChannel* tcp_channel)
// {
//     auto num_avail_bytes = tcp_channel->get_avail_input_length();
//     auto done = false;

//     do {
//         switch (msg_proto_state_) {
//         case MSGProtoState::READ_LENGTH:
//             myassert(!msg_len_);
//             if (num_avail_bytes >= IPCMsgProtocol::MSG_LEN_SIZE) {
//                 // read the bytes into our short
//                 auto rv = tcp_channel->read((uint8_t*)&msg_len_, IPCMsgProtocol::MSG_LEN_SIZE);
//                 myassert(rv == IPCMsgProtocol::MSG_LEN_SIZE);
//                 num_avail_bytes -= rv;
//                 // convert to host byte order
//                 msg_len_ = ntohs(msg_len_);
//                 // update state
//                 msg_proto_state_ = MSGProtoState::READ_MSG;
//             } else {
//                 done = true; // to break out of loop
//             }
//             break;
//         case MSGProtoState::READ_MSG:
//             myassert(msg_len_);
//             if (num_avail_bytes >= msg_len_) {
                
//                 // read protobuf msg

//             } else {
//                 done = true; // to break out of loop
//             }
//             break;
//         default:
//             myassert(false); // not reached
//         }
//     } while (!done);

//     // update read low watermark
//     _update_read_watermark();
// }

// void
// IPCClientChannel::_update_read_watermark()
// {
//     if (msg_proto_state_ == MSGProtoState::READ_LENGTH) {
//         myassert(!msg_len_);
//         tcp_channel_->set_read_watermark(IPCMsgProtocol::MSG_LEN_SIZE, 0);
//     } else {
//         myassert(msg_len_);
//         tcp_channel_->set_read_watermark(msg_len_, 0);
//     }
// }

// IPCClientChannel::~IPCClientChannel()
// {
// }

static uint32_t
s_next_instNum(void)
{
    static uint32_t next = 0;
    myassert(next < 0xfffff);
    return ++next;
}


JSONStreamChannel::JSONStreamChannel(
    StreamChannel::UniquePtr channel, JSONStreamChannelObserver* observer)
    : channel_(std::move(channel)), observer_(observer)
    , state_(StreamState::READ_TYPE_AND_LENGTH)
    , msg_type_(0), msg_len_(0)
    , instNum_(s_next_instNum())
{
    // set myself as observer of the underlying channel
    channel_->set_observer(this);
}

void
JSONStreamChannel::onNewReadDataAvailable(StreamChannel* channel) noexcept
{
    myassert(channel_.get() == channel);
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
            myassert(!msg_len_);
            if (num_avail_bytes >= (MSG_TYPE_SIZE + MSG_LEN_SIZE)) {
                auto rv = channel_->read((uint8_t*)&msg_type_, MSG_TYPE_SIZE);
                myassert(rv == MSG_TYPE_SIZE);
                num_avail_bytes -= rv;
                // convert to host byte order
                msg_type_ = ntohs(msg_type_);

                rv = channel_->read((uint8_t*)&msg_len_, MSG_LEN_SIZE);
                myassert(rv == MSG_LEN_SIZE);
                num_avail_bytes -= rv;
                // convert to host byte order
                msg_len_ = ntohs(msg_len_);

                // update state
                state_ = StreamState::READ_MSG;
            } else {
                done = true; // to break out of loop
            }
            break;
        case StreamState::READ_MSG:
            if (num_avail_bytes >= msg_len_) {
                DestructorGuard db(this);
                rapidjson::Document msg;

                if (msg_len_) {
                    uint8_t* buf = channel_->peek(msg_len_);
                    myassert(buf);
                    msg.Parse((const char*)buf, msg_len_);
                    channel_->drain(msg_len_);
                }

                // update state
                state_ = StreamState::READ_TYPE_AND_LENGTH;

                // notify
                observer_->onRecvMsg(this, msg_type_, msg);
            } else {
                done = true; // to break out of loop
            }
            break;
        default:
            myassert(false); // not reached
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

    myassert(sb.GetSize() < 0x3fff); // we realy shouldn't need
                                     // 16k-byte json msgs
    uint16_t len = sb.GetSize();

    _send_type_and_len(type, len);

    // make sure to write the right amount, i.e., "len" has already
    // been converted to network-byte order
    const auto rv = channel_->write((const uint8_t*)sb.GetString(), sb.GetSize());
    myassert(!rv);
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
    myassert(!rv);

    len = htons(len);
    rv = channel_->write((const uint8_t*)&len, sizeof(len));
    myassert(!rv);
}

void
JSONStreamChannel::_update_read_watermark()
{
    if (state_ == StreamState::READ_TYPE_AND_LENGTH) {
        myassert(!msg_len_);
        channel_->set_read_watermark(MSG_TYPE_SIZE + MSG_LEN_SIZE, 0);
    } else {
        myassert(msg_len_);
        channel_->set_read_watermark(msg_len_, 0);
    }
}

void
JSONStreamChannel::onEOF(StreamChannel* channel) noexcept
{
    myassert(channel_.get() == channel);
    state_ = StreamState::CLOSED;
    DestructorGuard db(this);
    puts("json stream notified of transport close");
    observer_->onEOF(this);
}

void
JSONStreamChannel::onError(StreamChannel* channel, int errorcode) noexcept
{
    myassert(channel_.get() == channel);
    state_ = StreamState::CLOSED;
    puts("json stream notified of transport error");
    DestructorGuard db(this);
    observer_->onError(this, errorcode);
}

JSONStreamChannel::~JSONStreamChannel()
{
}


}
