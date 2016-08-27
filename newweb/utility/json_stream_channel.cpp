
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
    , state_(StreamState::READ_LENGTH)
    , instNum_(s_next_instNum())
{
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
        case StreamState::READ_LENGTH:
            myassert(!msg_len_);
            if (num_avail_bytes >= MSG_LEN_SIZE) {
                // read the bytes into our short
                static const auto num_to_read = MSG_LEN_SIZE;
                const auto rv = channel_->read((uint8_t*)&msg_len_, num_to_read);
                myassert(rv == num_to_read);
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
            myassert(msg_len_);
            if (num_avail_bytes >= msg_len_) {
                DestructorGuard db(this);
                uint8_t* buf = channel_->peek(msg_len_);
                myassert(buf);

                rapidjson::Document msg;
                msg.Parse((const char*)buf, msg_len_);

                // notify
                observer_->onRecvMsg(this, msg);

                channel_->drain(msg_len_);
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
JSONStreamChannel::sendMsg(const rapidjson::Document& msg)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    msg.Accept(writer);

    myassert(sb.GetSize() < 0xffff);
    uint16_t len = sb.GetSize();
    len = htons(len);

    auto rv = channel_->write((const uint8_t*)&len, sizeof(len));
    myassert(!rv);

    rv = channel_->write((const uint8_t*)sb.GetString(), sb.GetSize());
    myassert(!rv);
}

void
JSONStreamChannel::_update_read_watermark()
{
    if (state_ == StreamState::READ_LENGTH) {
        myassert(!msg_len_);
        channel_->set_read_watermark(MSG_LEN_SIZE, 0);
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
    observer_->onEOF(this);
}

void
JSONStreamChannel::onError(StreamChannel* channel, int errorcode) noexcept
{
    myassert(channel_.get() == channel);
    state_ = StreamState::CLOSED;
    DestructorGuard db(this);
    observer_->onError(this, errorcode);
}

JSONStreamChannel::~JSONStreamChannel()
{
}


}
