#ifndef json_stream_channel_hpp
#define json_stream_channel_hpp


#include <event2/event.h>
#include <event2/bufferevent.h>
#include <boost/core/noncopyable.hpp>
#include <memory>
#include <functional>
#include <rapidjson/document.h>

#include "stream_channel.hpp"

using myio::StreamChannel;

namespace myio
{

// class IPCChannel : public folly::DelayedDestruction
// {

// };

// class IPCClientChannel : public folly::DelayedDestruction
//                        , public myio::TCPChannelConnectObserver
//                        , public myio::TCPChannelObserver
// {
// public:

//     typedef std::unique_ptr<IPCClientChannel, /*folly::*/Destructor> UniquePtr;

//     explicit IPCClientChannel(struct event_base*,
//                               const in_addr_t& addr, const in_port_t& port,
//                               IPCChannelObserver*);

//     /* return true if successfully initiated a connect attempt. won't
//      * be connected until onConnected() is called. */
//     bool start_connecting();

// private:

//     virtual ~IPCClientChannel();

//     /********* TCPChannelConnectObserver interface *************/
//     virtual void onConnected(myio::TCPChannel*) noexcept override;
//     virtual void onConnectError(myio::TCPChannel*, int errorcode) noexcept override;

//     /********* TCPChannelObserver interface *************/
//     virtual void onNewReadDataAvailable(myio::TCPChannel*) noexcept override;
//     virtual void onEOF(myio::TCPChannel*) noexcept override {};
//     /* will be called on any kind of error, whether read or write */
//     virtual void onError(myio::TCPChannel*, int errorcode) noexcept override {};

//     void _consume_input(myio::TCPChannel*);
//     void _update_read_watermark();


//     ///////////
//     myio::TCPClientChannel::UniquePtr tcp_channel_;

//     enum class MSGProtoState {
//         READ_LENGTH, READ_MSG
//     } msg_proto_state_;

//     uint16_t msg_len_; // parsed from input
// };


/*
 * represents a stream channel that deals with messages that are in
 * json format.
 *
 * each msg must have:
 *
 * - a header that contains: 2-byte type field and 2-byte len field
 * (both in network byte order). the type field is not interpreted
 * here; its semantic is up to the observer on top of this channel.
 *
 * - the msg payload, which should be the serialization of a json
 * object. the length in bytes of this serialized string is specified
 * by the 2-byte len field mentioned above.
 *
 * if the length field contains value zero, then an empty json object
 * will be sent up to observer
 */

class JSONStreamChannel;

/* interface to notify observer */
class JSONStreamChannelObserver
{
public:
    /* "type" is the 2-byte type in the header mentioned above */
    virtual void onRecvMsg(JSONStreamChannel*, uint16_t type,
                           const rapidjson::Document&) noexcept = 0;
    virtual void onEOF(JSONStreamChannel*) noexcept = 0;
    virtual void onError(JSONStreamChannel*, int errorcode) noexcept = 0;
};

/* layered on top of an underlying stream channel, read its data and
 * extract json msgs out of the underlying stream and send those msgs
 * to the observer.
 *
 * we're like a filter in a pipeline; maybe can use something like
 * facebook's wangle framework?
 *
 */
class JSONStreamChannel : public folly::DelayedDestruction
                        , public myio::StreamChannelObserver
{
public:
    typedef std::unique_ptr<JSONStreamChannel, /*folly::*/Destructor> UniquePtr;

    /* will take ownership of the stream channel */
    explicit JSONStreamChannel(StreamChannel::UniquePtr, JSONStreamChannelObserver*);

    void sendMsg(uint16_t type, const rapidjson::Document&);
    void sendMsg(uint16_t type); // send empty msg

    const uint32_t& instNum() const { return instNum_; }

protected:

    // the msg type is two bytes, in network byte order
    static const int MSG_TYPE_SIZE = 2;
    // the msg len is two bytes, in network byte order
    static const int MSG_LEN_SIZE = 2;

    /* keep the destructor protected/private to prevent direct
     * deletion; use DelayedDestruction's destroy() method */
    virtual ~JSONStreamChannel();

    /********* StreamChannelObserver interface *************/
    virtual void onNewReadDataAvailable(StreamChannel*) noexcept override;
    virtual void onEOF(StreamChannel*) noexcept override;
    virtual void onError(StreamChannel*, int errorcode) noexcept override;
    virtual void onWrittenData(StreamChannel*) noexcept override {};

    void _consume_input();
    void _update_read_watermark();
    void _send_type_and_len(uint16_t type, uint16_t len);

    StreamChannel::UniquePtr channel_; // the underlying stream
    JSONStreamChannelObserver* observer_; // dont free

    enum class StreamState {
        READ_TYPE_AND_LENGTH, READ_MSG, CLOSED
    } state_;

    uint16_t msg_type_;
    uint16_t msg_len_; // length of the next msg we're waiting for in
                       // the input stream

    const uint32_t instNum_;
};


} // end myio namespace

#endif /* json_stream_channel_hpp */
