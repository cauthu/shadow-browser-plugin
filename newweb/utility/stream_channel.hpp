#ifndef stream_channel_hpp
#define stream_channel_hpp


#include <event2/buffer.h>
#include "DelayedDestruction.h"
#include <memory>

#include "object.hpp"


namespace myio
{

class StreamChannel;

class StreamChannelConnectObserver
{
public:
    /* to notify user that its (client) channel is now connected */
    virtual void onConnected(StreamChannel*) noexcept = 0;

    virtual void onConnectError(StreamChannel*, int errorcode) noexcept = 0;
};

class StreamChannelObserver
{
public:

    /* tell user that there is new data that can be read, without
     * saying how much. there might still be existing buffered data
     * that user has not read. */
    virtual void onNewReadDataAvailable(StreamChannel*) noexcept = 0;

    virtual void onWrittenData(StreamChannel*) noexcept = 0;

    /* on error or eof, any buffered input data can still be read if
     * the user wants to.
     */

    virtual void onEOF(StreamChannel*) noexcept = 0;
    /* will be called on any kind of error, whether read or write */
    virtual void onError(StreamChannel*, int errorcode) noexcept = 0;
};

class StreamChannel : public Object
{
public:
    // for convenience. DelayedDestruction (see folly's
    // AsyncTransport.h for example)
    typedef std::unique_ptr<StreamChannel, folly::DelayedDestruction::Destructor> UniquePtr;

    virtual int start_connecting(StreamChannelConnectObserver*) = 0;

    virtual void set_observer(StreamChannelObserver*) = 0;

    /* obtain up to "len" bytes of input data (i.e., received from
     * other end point of channel). return the number of bytes that
     * were read
     * 
     * the returned data is removed from the stream's buffer
     */
    virtual size_t read(uint8_t *data, size_t len) = 0;
    /* obtain all available input data. return 0 on success, -1 on
     * failure */
    virtual int read_buffer(struct evbuffer* buf) = 0;
    /* just drop up to "len" bytes of the input data. returns 0 on
     * success, -1 on error */
    virtual int drain(size_t len) = 0;

    /* obtain a pointer to the available input data. "len" is how much
     * want to peek at, use "-1" to peek at all currently available
     * input data. "len" should not be greater than the number of
     * bytes currently available.
     *
     * the data remains in the stream's buffer. 
     */
    virtual uint8_t* peek(size_t len) = 0;

    /* get number of availabe input bytes */
    virtual size_t get_avail_input_length() const = 0;

    /* same as bufferevent_setwatermark() for read */
    virtual void set_read_watermark(size_t lowmark, size_t highmark) = 0;


    /* same to output. same as libevent's bufferevent_write calls. in
     * particular, any write before channel is established will be
     * buffered */
    virtual int write(const uint8_t *data, size_t len) = 0;
    virtual int write_buffer(struct evbuffer *buf) = 0;


    /* close/disconnect the channel, dropping pending/buffered data if
     * any */
    virtual void close() = 0;
};

}

#endif /* stream_channel_hpp */
