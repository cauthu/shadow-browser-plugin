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
    virtual void onRecvMsg(GenericMessageChannel*, uint8_t type, uint32_t id,
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
class GenericMessageChannel : public StreamChannelObserver
                            , public Object
{
public:
    typedef std::unique_ptr<GenericMessageChannel, /*folly::*/Destructor> UniquePtr;

    /* will take ownership of the stream channel
     *
     * assumes that the channel is already connected, ready for data
     * exchange, etc. i.e., will not call start_connecting()
     */
    explicit GenericMessageChannel(StreamChannel::UniquePtr,
                                   GenericMessageChannelObserver*);

    void sendMsg(uint8_t type, uint16_t len, const uint8_t* data,
                 uint32_t id=0);
    /* send empty msg, i.e., equivalent to sendMsg(type, 0, nullptr,
     * id) */
    void sendMsg(uint8_t type, uint32_t id=0);

protected:

    static const int MSG_TYPE_SIZE = sizeof (uint8_t);
    static const int MSG_ID_SIZE = sizeof (uint32_t);
    static const int MSG_LEN_SIZE = sizeof (uint16_t);

    /* keep the destructor protected/private to prevent direct
     * deletion; use DelayedDestruction's destroy() method */
    virtual ~GenericMessageChannel() = default;

    /********* StreamChannelObserver interface *************/
    virtual void onNewReadDataAvailable(StreamChannel*) noexcept override;
    virtual void onEOF(StreamChannel*) noexcept override;
    virtual void onError(StreamChannel*, int errorcode) noexcept override;
    virtual void onWrittenData(StreamChannel*) noexcept override {};

    ////////

    void _consume_input();
    void _update_read_watermark();
    void _send_header(uint8_t type, uint32_t id, uint16_t len);

    StreamChannel::UniquePtr channel_; // the underlying stream
    GenericMessageChannelObserver* observer_; // dont free
    const bool with_msg_id_;
    const size_t header_size_;

    enum class StreamState {
        READ_HEADER, READ_MSG, CLOSED
    } state_;

    /* header fields of current message we're extracting. msg_id_ is
     * optional, depends on whether with_msg_id_ */
    uint8_t msg_type_;
    uint32_t msg_id_;
    uint16_t msg_len_;
};


} // end myio namespace

#endif /* generic_message_channel_hpp */
