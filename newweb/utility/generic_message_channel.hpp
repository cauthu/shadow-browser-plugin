#ifndef generic_message_channel_hpp
#define generic_message_channel_hpp


#include <event2/event.h>
#include <event2/bufferevent.h>
#include <memory>
#include <functional>

#include "object.hpp"
#include "stream_channel.hpp"

using myio::StreamChannel;

namespace myio
{

/*
 * represents a stream channel that deals with opaque messages
 *
 * each msg must have:
 *
 * - a header that contains: 2-byte type field and 2-byte len field
 * (both in network byte order). the type field is not interpreted
 * here; its semantic is up to the observer on top of this channel.
 *
 * - the msg payload. the length in bytes of this message string is
 * specified by the 2-byte len field mentioned above.
 *
 * if the length field contains value zero, then a nullptr will be
 * sent up to observer along with the type
 */

class GenericMessageChannel;

/* interface to notify observer */
class GenericMessageChannelObserver
{
public:
    /* "type" is the 2-byte type in the header mentioned above */
    virtual void onRecvMsg(GenericMessageChannel*, uint16_t type,
                           uint16_t len, const uint8_t*) noexcept = 0;
    virtual void onEOF(GenericMessageChannel*) noexcept = 0;
    virtual void onError(GenericMessageChannel*, int errorcode) noexcept = 0;
};

/* layered on top of an underlying stream channel -- i.e., it HAS, not
 * IS, a stream channel -- read its data and extract msgs out of the
 * underlying stream and send those msgs to the observer.
 *
 * we're like a filter in a pipeline; maybe can use something like
 * facebook's wangle framework?
 *
 */
class GenericMessageChannel : public myio::StreamChannelObserver
                            , public Object
{
public:
    typedef std::unique_ptr<GenericMessageChannel, /*folly::*/Destructor> UniquePtr;

    /* will take ownership of the stream channel */
    explicit GenericMessageChannel(StreamChannel::UniquePtr, GenericMessageChannelObserver*);

    void sendMsg(uint16_t type, uint16_t len, const uint8_t* data);
    void sendMsg(uint16_t type); // send empty msg

protected:

    // the msg type is two bytes, in network byte order
    static const int MSG_TYPE_SIZE = 2;
    // the msg len is two bytes, in network byte order
    static const int MSG_LEN_SIZE = 2;

    static const int HEADER_SIZE = MSG_TYPE_SIZE + MSG_LEN_SIZE;

    /* keep the destructor protected/private to prevent direct
     * deletion; use DelayedDestruction's destroy() method */
    virtual ~GenericMessageChannel() = default;

    /********* StreamChannelObserver interface *************/
    virtual void onNewReadDataAvailable(StreamChannel*) noexcept override;
    virtual void onEOF(StreamChannel*) noexcept override;
    virtual void onError(StreamChannel*, int errorcode) noexcept override;
    virtual void onWrittenData(StreamChannel*) noexcept override {};

    void _consume_input();
    void _update_read_watermark();
    void _send_type_and_len(uint16_t type, uint16_t len);

    StreamChannel::UniquePtr channel_; // the underlying stream
    GenericMessageChannelObserver* observer_; // dont free

    enum class StreamState {
        READ_TYPE_AND_LENGTH, READ_MSG, CLOSED
    } state_;

    /* type and len of the current message we're extracting */
    uint16_t msg_type_;
    uint16_t msg_len_;
};


} // end myio namespace

#endif /* generic_message_channel_hpp */
