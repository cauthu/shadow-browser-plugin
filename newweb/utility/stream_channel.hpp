#ifndef stream_channel_hpp
#define stream_channel_hpp

#include <event2/buffer.h>
#include <memory>

#include "object.hpp"
#include "folly/DelayedDestruction.h"


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

class StreamChannelInputDropObserver
{
public:
    /* this will be called either ONCE when the channel has now
     * dropped all the bytes requested in latest drop_future_input(),
     * or possibly multiple times, depending on the "notify_progress"
     * specified in the request.
     *
     * "len" will be the number of bytes that has been dropped since
     * last notification, or the whole number of bytes that was
     * specified in the request if progress notification was not
     * requested.
     */
    virtual void onInputBytesDropped(StreamChannel*, size_t len) noexcept = 0;

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

};

class StreamChannel : public Object
{
public:
    // for convenience. DelayedDestruction (see folly's
    // AsyncTransport.h for example)
    typedef std::unique_ptr<StreamChannel, /*folly::DelayedDestruction::*/Destructor> UniquePtr;

    /* optional "connect_timeout" is how long to wait for the connect
     * to succeed and calls onConnectTimeout() if it times out
     */
    virtual int start_connecting(StreamChannelConnectObserver*,
                                 struct timeval *connect_timeout=nullptr) = 0;

    virtual void set_observer(StreamChannelObserver* o) { observer_ = o; }
    virtual StreamChannelObserver* observer(StreamChannelObserver* o) const { return observer_; }

    /* give a hint to channel on how much to read from socket at a
     * time
     *
     * -1 means no hint, so totally up to channel.
     *
     * 0 means read all available data from socket every time
     *
     * > 0 means to try to read a specific amount
     */
    virtual void set_read_size_hint(int len);

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

    /* get to the underlying input buffer maintained by the channel.
     *
     * of course user should only read data from this buffer, and not
     * add data to it.
     */
    virtual struct evbuffer* get_input_evbuf() = 0;

    /* request the channel to just ingore the next "len" bytes from
     * the socket/network, i.e., do not put them in the input buffer
     * that's visible to the application.
     *
     * NOTE: this only drop FUTURE bytes from socket; data already in
     * the input buf is not affected. user will have to use drain() as
     * appropriate.
     *
     * only one such request at a time, i.e., if this is called when
     * the channel has not finished with a previous request, it might
     * throw/crash
     *
     * "notify_progress": false -> when the channel has fulfilled the
     * whole request, it will notify. true -> notify as bytes are
     * dropped
     */
    virtual void drop_future_input(StreamChannelInputDropObserver*,
                                   size_t len, bool notify_progress) = 0;

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

    /* write "len" bytes of dummy data. for now it should be all
     * ascii. return 0 on success */
    virtual int write_dummy(size_t len) = 0;

    /* close/disconnect the channel, dropping pending/buffered data if
     * any
     *
     * be careful when calling close() while being called back. e.g.,
     * if you close while handling onNewReadDataAvailable(), you can
     * cause crash because close() might free stuff that the channel
     * might try to use after you return from onNewReadDataAvailable()
     */
    virtual void close() = 0;

    virtual bool is_closed() const = 0;

    virtual int release_fd() = 0;

    virtual const size_t& num_total_read_bytes() const final
    {
        return num_total_read_bytes_;
    }
    virtual const size_t& num_total_written_bytes() const final
    {
        return num_total_written_bytes_;
    }

protected:

    StreamChannel(StreamChannelObserver*);

    virtual ~StreamChannel() = default;

    StreamChannelObserver *observer_;
    int read_size_hint_;

    // total num of bytes read from and written to socket
    size_t num_total_read_bytes_;
    size_t num_total_written_bytes_;
};

}

#endif /* stream_channel_hpp */
