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

typedef boost::function<void(BufloMuxChannel*, bool)> ChannelPairResultCb;
typedef boost::function<void(BufloMuxChannel*)> ChannelClosedCb;

typedef boost::function<void(BufloMuxChannel*, bool, int)> StreamCreateResultCb;
typedef boost::function<void(BufloMuxChannel*, int)> StreamDataCb;
typedef boost::function<void(BufloMuxChannel*, int)> StreamClosedCb;


class BufloMuxChannel : public Object
{
public:
    // for convenience. DelayedDestruction (see folly's
    // AsyncTransport.h for example)
    typedef std::unique_ptr<BufloMuxChannel, Destructor> UniquePtr;

    virtual int create_stream(const char* host,
                              const in_port_t& port) = 0;

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

    // /* request the channel to just ingore the next "len" bytes from
    //  * the socket/network, i.e., do not put them in the input buffer
    //  * that's visible to the application.
    //  *
    //  * only one such request at a time, i.e., if this is called when
    //  * the channel has not finished with a previous request, it might
    //  * throw/crash
    //  *
    //  * "notify_progress": false -> when the channel has fulfilled the
    //  * whole request, it will notify. true -> notify as bytes are
    //  * dropped
    //  */
    // virtual void drop_future_input(int sid,
    //                                BufloMuxChannelInputDropObserver*,
    //                                size_t len, bool notify_progress) = 0;

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
                    ChannelPairResultCb ch_pair_result_cb,
                    ChannelClosedCb ch_closed_cb,
                    StreamCreateResultCb st_create_result_cb,
                    StreamDataCb st_data_cb,
                    StreamClosedCb st_closed_cb)
        : fd_(fd), is_client_side_(is_client_side)
        , ch_pair_result_cb_(ch_pair_result_cb)
        , ch_closed_cb_(ch_closed_cb)
        , st_create_result_cb_(st_create_result_cb)
        , st_data_cb_(st_data_cb)
        , st_closed_cb_(st_closed_cb)
    {
        CHECK_GT(fd_, 0);
        CHECK(ch_pair_result_cb_);
        CHECK(ch_closed_cb_);
        CHECK(st_data_cb_);
        CHECK(st_closed_cb_);
        if (is_client_side_) {
            CHECK(st_create_result_cb_);
        }
    }

    virtual ~BufloMuxChannel() = default;

    ChannelPairResultCb ch_pair_result_cb_;
    ChannelClosedCb ch_closed_cb_;

    StreamCreateResultCb st_create_result_cb_;
    StreamDataCb st_data_cb_;
    StreamClosedCb st_closed_cb_;

    int fd_;
    const bool is_client_side_;

    /* the repeating timer used for buflo */
    Timer::UniquePtr buflo_timer_;
};

}
}

#endif /* bulfo_mux_channel_hpp */
