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

/* callback interface */
class BufloMuxChannelStreamObserver
{
public:

    /* for notifying callers of create_stream(), which should only be
     * used by client-side proxy
     */
    virtual void onStreamIdAssigned(BufloMuxChannel*, int sid) = 0;
    virtual void onStreamCreateResult(BufloMuxChannel*, bool,
                                      const in_addr_t&, const uint16_t&) = 0;

    /* for streams for either side */
    virtual void onStreamNewDataAvailable(BufloMuxChannel*, int sid) = 0;

    /* there will be no more data available after being notified of
     * this, but the stream is not closed yet. i.e., it's only half
     * closed.
     *
     * you should still be able to send into the stream. NOT TESTED
     * yet
     */
    virtual void onStreamRecvEOF(BufloMuxChannel*, int sid) = 0;

    virtual void onStreamClosed(BufloMuxChannel*, int sid) = 0;
};

class BufloMuxChannel : public Object
{
public:
    // for convenience. DelayedDestruction (see folly's
    // AsyncTransport.h for example)
    typedef std::unique_ptr<BufloMuxChannel, Destructor> UniquePtr;


    enum class ChannelStatus : short
    {
        READY,
        A_DEFENSE_SESSION_DONE /* i.e., both send and recv directions
                                * are done */,
        CLOSED
    };
    typedef boost::function<void(BufloMuxChannel*, ChannelStatus)> ChannelStatusCb;

    /*
     * tell user that the channel wants to create a new stream with id
     * "sid" to connect to the target
     *
     * the user should call set_stream_connect_result() to tell the
     * channel about the result
     */
    typedef boost::function<void(BufloMuxChannel*, int sid,
                                 const char* host, uint16_t port)> NewStreamConnectRequestCb;


    virtual int create_stream(const char* host,
                              const in_port_t& port,
                              void *cbdata) = 0;

    virtual int create_stream2(const char* host,
                              const in_port_t& port,
                              BufloMuxChannelStreamObserver*) = 0;

    /* these are the same parameters as in the Tamaraw paper */
    virtual bool start_defense_session() = 0;
    /* schedule the defense to stop when it has reached sending the
     * next multiple of L.
     *
     * but if "right_now" is true, then will stop right now
     */
    virtual void stop_defense_session(bool right_now=false) = 0;

    /* user wants to be defended, but only once he next actually has
     * useful activity. "start_defense_session()" immediately starts
     * defense session, and if user doesn't have data to send yet then
     * it's wasteful because the channel will be sending dummy data
     *
     * after the defense session has stopped (on its own or
     * stop_defense_session()), user has to call this again to
     * re-enable this behavior
     */
    virtual void set_auto_start_defense_session_on_next_send() = 0;

    virtual bool set_stream_observer(int sid, BufloMuxChannelStreamObserver*) = 0;
    /* tell the channel that the stream connect request has
     * succeeded. if it has failed, the user can just call
     * close_stream() */
    virtual bool set_stream_connected(int sid) = 0;

    /* obtain up to "len" bytes of input data (i.e., received from
     * other end point of channel).
     *
     * The return value is -1 on failure, and is otherwise the number
     * of bytes copied.
     * 
     * the returned data is removed from the stream's buffer
     */
    // virtual int read(int sid, uint8_t *data, size_t len) = 0;

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
    // virtual int write(int sid, const uint8_t *data, size_t len) = 0;

    /* returns 0 on success, -1 on failure. */
    virtual int write_buffer(int sid, struct evbuffer *buf) = 0;

    /* tell the buflo channel that the there will be no more data to
     * write for the stream id. the buflo channel will continue to
     * write any data still buffered for this stream
     */
    virtual int set_write_eof(int sid) = 0;

    /* write "len" bytes of dummy data. for now it should be all
     * ascii */
    // virtual int write_dummy(int sid, size_t len) = 0;

    /* close/disconnect the channel, dropping pending/buffered data if
     * any.
     *
     * the user will not be notified of any further activity,
     * including "onStreamClosed" by the stream
     */
    virtual void close_stream(int sid) = 0;

    // virtual bool is_closed() const = 0;

protected:

    BufloMuxChannel(int fd,
                    bool is_client_side,
                    ChannelStatusCb ch_status_cb,
                    NewStreamConnectRequestCb st_connect_req_cb)
        : fd_(fd), is_client_side_(is_client_side)
        , ch_status_cb_(ch_status_cb)
        , st_connect_req_cb_(st_connect_req_cb)
    {
        CHECK_GT(fd_, 0);
        CHECK(ch_status_cb_);
        if (is_client_side_) {
            CHECK(!st_connect_req_cb);
        } else {
            CHECK(st_connect_req_cb);
        }
    }

    virtual ~BufloMuxChannel() = default;

    ChannelStatusCb ch_status_cb_;
    NewStreamConnectRequestCb st_connect_req_cb_;

    int fd_;
    const bool is_client_side_;

    /* the repeating timer used for buflo */
    Timer::UniquePtr buflo_timer_;
};

}
}

#endif /* bulfo_mux_channel_hpp */
