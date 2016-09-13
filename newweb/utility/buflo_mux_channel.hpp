#ifndef bulfo_mux_channel_hpp
#define bulfo_mux_channel_hpp

#include <memory>
#include <boost/function.hpp>

#include "easylogging++.h"
#include "object.hpp"
#include "timer.hpp"


namespace myio { namespace buflo
{

class BufloMuxChannel;

typedef boost::function<void(BufloMuxChannel*)> ChannelClosedCb;

/*
 * tell user that the channel wants to create a new stream with id
 * "sid" to connect to the target
 *
 * the user should call set_stream_connect_result() to tell the
 * channel about the result
 */
typedef boost::function<void(BufloMuxChannel*, int sid,
                             const char* host, uint16_t port)> NewStreamConnectRequestCb;

/*
 * call back to tell user about the stream id has been assigned for
 * one of their earlier create_stream() calls
 *
 * the "void cbdata" is the value passed into create_stream() call
 */
// typedef boost::function<void(BufloMuxChannel*, int, void* cbdata)> StreamIdAssignedCb;

/*
 * call back to tell user about result of stream connect id. the bool
 * arg specifies whether the stream creation is successful, in which
 * case the int is the stream id that should be used in calls to other
 * apis.
 */
// typedef boost::function<void(BufloMuxChannel*, int, bool)> StreamCreateResultCb;

// typedef boost::function<void(BufloMuxChannel*, int)> StreamDataCb;
// typedef boost::function<void(BufloMuxChannel*, int)> StreamClosedCb;

/* callback interface */
class BufloMuxChannelStreamObserver
{
public:

    /* for notifying callers of create_stream(), which should only be
     * used by client-side proxy
     */
    virtual void onStreamIdAssigned(BufloMuxChannel*, int sid) = 0;
    virtual void onStreamCreateResult(BufloMuxChannel*, bool) = 0;

    // /* for notifying server-side proxy of new (client) stream connect
    //  * requests
    //  *
    //  * "sid" is stream id
    //  *
    //  * port is HOST-byte order
    //  *
    //  * the observer should tell the buflomuxchannel the result of the request with 
    //  */
    // virtual void onNewStreamRequest(BufloMuxChannel*, int sid,
    //                                 const char* host, uint16_t port) = 0;

    /* for streams for either side */
    virtual void onStreamNewDataAvailable(BufloMuxChannel*) = 0;
    virtual void onStreamClosed(BufloMuxChannel*) = 0;
};

class BufloMuxChannel : public Object
{
public:
    // for convenience. DelayedDestruction (see folly's
    // AsyncTransport.h for example)
    typedef std::unique_ptr<BufloMuxChannel, Destructor> UniquePtr;

    virtual int create_stream(const char* host,
                              const in_port_t& port,
                              void *cbdata) = 0;

    virtual bool set_stream_observer(int sid, BufloMuxChannelStreamObserver*) = 0;
    virtual bool set_stream_connect_result(int sid, bool ok) = 0;

    /* obtain up to "len" bytes of input data (i.e., received from
     * other end point of channel).
     *
     * The return value is -1 on failure, and is otherwise the number
     * of bytes copied.
     * 
     * the returned data is removed from the stream's buffer
     */
    virtual int read(int sid, uint8_t *data, size_t len) = 0;

    /* returns the number of bytes moved. */
    virtual int read_buffer(int sid, struct evbuffer* buf, size_t len) = 0;

    /* just drop up to "len" bytes of the input data.
     *
     * returns 0 on success and -1 on failure.
     */
    virtual int drain(int sid, size_t len) = 0;

    /* obtain a pointer to the available input data. "len" is how much
     * want to peek at, use -1 to peek at all currently available
     * input data. "len" should not be greater than the number of
     * bytes currently available (can be obtained with
     * get_avail_input_length())
     *
     * the data remains in the stream's buffer. 
     */
    virtual uint8_t* peek(int sid, ssize_t len) = 0;

    /* get to the underlying input buffer maintained by the channel.
     *
     * of course user should only read data from this buffer, and not
     * add data to it.
     */
    virtual struct evbuffer* get_input_evbuf(int sid) = 0;

    /* get number of availabe input bytes */
    virtual size_t get_avail_input_length(int sid) const = 0;
    /* get number of buffered output bytes */
    // virtual size_t get_output_length(int sid) const = 0;

    // /* same as bufferevent_setwatermark() for read */
    // virtual void set_read_watermark(int sid,
    //                                 size_t lowmark,
    //                                 size_t highmark) = 0;


    /* any write before channel is established will be buffered,
     * assuming in buffered mode.
     *
     * returns 0 on success, and -1 on failure.
     */
    virtual int write(int sid, const uint8_t *data, size_t len) = 0;

    /* returns 0 on success, -1 on failure. */
    virtual int write_buffer(int sid, struct evbuffer *buf) = 0;

    /* write "len" bytes of dummy data. for now it should be all
     * ascii */
    virtual int write_dummy(int sid, size_t len) = 0;

    /* close/disconnect the channel, dropping pending/buffered data if
     * any */
    virtual void close_stream(int sid) = 0;

    // virtual bool is_closed() const = 0;

protected:

    BufloMuxChannel(int fd,
                    bool is_client_side,
                    ChannelClosedCb ch_closed_cb,
                    NewStreamConnectRequestCb st_connect_req_cb)
        : fd_(fd), is_client_side_(is_client_side)
        , ch_closed_cb_(ch_closed_cb)
        , st_connect_req_cb_(st_connect_req_cb)
    {
        CHECK_GT(fd_, 0);
        CHECK(ch_closed_cb_);
        if (is_client_side_) {
            CHECK(!st_connect_req_cb);
        } else {
            CHECK(st_connect_req_cb);
        }
    }

    virtual ~BufloMuxChannel() = default;

    ChannelClosedCb ch_closed_cb_;
    NewStreamConnectRequestCb st_connect_req_cb_;

    int fd_;
    const bool is_client_side_;

    /* the repeating timer used for buflo */
    Timer::UniquePtr buflo_timer_;
};

}
}

#endif /* bulfo_mux_channel_hpp */
