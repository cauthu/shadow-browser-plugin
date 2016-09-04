#ifndef stream_channel_hpp
#define stream_channel_hpp

#include <sys/time.h>
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

    virtual void onConnectTimeout(StreamChannel*) noexcept = 0;
};

class StreamChannelObserver
{
public:

    /************ if the channel is in buffered mode **************/

    /* tell user that there is new data that can be read, without
     * saying how much. there might still be existing buffered data
     * that user has not read. */
    virtual void onNewReadDataAvailable(StreamChannel*) noexcept = 0;

    /* tell user that enough data has been written: when a write has
     * occured that takes the output buffer under the write low-water
     * mark, this call back is invoked. by default, the write
     * low-water mark is zero, so this callback is called when the
     * output buffer has been emptied.
     */
    virtual void onWrittenData(StreamChannel*) noexcept = 0;

    /* on error or eof, any buffered input data can still be read if
     * the user wants to.
     */

    virtual void onEOF(StreamChannel*) noexcept = 0;
    /* will be called on any kind of error, whether read or write */
    virtual void onError(StreamChannel*, int errorcode) noexcept = 0;


    // /******* if the channel is in non-buffered mode *******/

    // /* these notify the observer so that observer can call the
    //  * non-buffered versions of read/write */
    // virtual void onReadable(StreamChannel*) noexcept = 0;
    // virtual void onWritable(StreamChannel*) noexcept = 0;

};

class StreamChannel : public Object
{
public:
    // for convenience. DelayedDestruction (see folly's
    // AsyncTransport.h for example)
    typedef std::unique_ptr<StreamChannel, folly::DelayedDestruction::Destructor> UniquePtr;

    virtual int start_connecting(StreamChannelConnectObserver*,
                                 struct timeval *connect_timeout=nullptr) = 0;

    virtual void set_observer(StreamChannelObserver*) = 0;
    // virtual int set_non_buffer_mode(bool) = 0;

    /* obtain up to "len" bytes of input data (i.e., received from
     * other end point of channel).
     *
     * The return value is -1 on failure, and is otherwise the number
     * of bytes copied.
     * 
     * the returned data is removed from the stream's buffer
     */
    virtual int read(uint8_t *data, size_t len) = 0;

    /* returns the number of bytes moved. */
    virtual int read_buffer(struct evbuffer* buf, size_t len) = 0;

    /* just drop up to "len" bytes of the input data.
     *
     * returns 0 on success and -1 on failure.
     */
    virtual int drain(size_t len) = 0;

    /* obtain a pointer to the available input data. "len" is how much
     * want to peek at, use -1 to peek at all currently available
     * input data. "len" should not be greater than the number of
     * bytes currently available (can be obtained with
     * get_avail_input_length())
     *
     * the data remains in the stream's buffer. 
     */
    virtual uint8_t* peek(ssize_t len) = 0;

    // /* for use in non-buffered mode.
    //  *
    //  * On success, the number of bytes read is returned (zero
    //  * indicates end of file). On error, -1 is returned, and errno is
    //  * set appropriately.
    //  */
    // virtual int nb_read(uint8_t *data, size_t len) = 0;

    // /* for use in non-buffered mode.
    //  *
    //  * if "len" is negative, then the library will guess how much to
    //  * read
    //  *
    //  * returns number of bytes read on success, 0 on EOF, and -1 on an
    //  * error (with errno set appropriately)
    //  */
    // virtual int nb_read_buffer(struct evbuffer* buf, int len) = 0;

    virtual struct evbuffer* get_input_evbuf() = 0;

    /* get number of availabe input bytes */
    virtual size_t get_avail_input_length() const = 0;
    /* get number of buffered output bytes */
    virtual size_t get_output_length() const = 0;

    /* same as bufferevent_setwatermark() for read */
    virtual void set_read_watermark(size_t lowmark, size_t highmark) = 0;


    /* any write before channel is established will be buffered,
     * assuming in buffered mode.
     *
     * returns 0 on success, and -1 on failure.
     */
    virtual int write(const uint8_t *data, size_t len) = 0;

    /* returns 0 on success, -1 on failure. */
    virtual int write_buffer(struct evbuffer *buf) = 0;

    // /* for use in non-buffered mode.
    //  *
    //  * On success, the number of bytes written is returned (zero
    //  * indicates nothing was written).  On error, -1 is returned, and
    //  * errno is set appropriately.
    //  */
    // virtual ssize_t nb_write(uint8_t *data, size_t len) = 0;

    // /* for use in non-buffered mode.
    //  *
    //  * returns a number of bytes written on success, and -1 on
    //  * failure.
    //  */
    // virtual int nb_write_buffer(struct evbuffer* buf) = 0;
    
    // /* this evbuf contains data to be sent to remote endpoint. you can
    //  * append data into this evbuffer if you don't want to use the
    //  * write/write_buffer calls above.
    //  *
    //  * NOTE! may only add (not remove) data from the output buffer.
    //  */
    // virtual struct evbuffer* get_output_evbuf() = 0;

    /* close/disconnect the channel, dropping pending/buffered data if
     * any */
    virtual void close() = 0;

    virtual bool is_closed() const = 0;

protected:

    virtual ~StreamChannel() = default;
};

}

#endif /* stream_channel_hpp */
