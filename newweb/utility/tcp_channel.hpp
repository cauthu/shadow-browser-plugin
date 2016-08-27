#ifndef tcp_channel_hpp
#define tcp_channel_hpp


#include <event2/bufferevent.h>
#include <boost/core/noncopyable.hpp>
#include <memory>
#include <functional>

#include <folly/io/async/DelayedDestruction.h>

#include "stream_channel.hpp"

namespace myio
{

class TCPChannel;


/* interface to group various callbacks related to channel for the
 * channel to notify its user. implement this interface in order to
 * use TCPChannel's classes.
 */

class TCPChannelConnectObserver
{
public:
    /* to notify user that its (client) channel is now connected */
    virtual void onConnected(TCPChannel*) noexcept = 0;

    virtual void onConnectError(TCPChannel*, int errorcode) noexcept = 0;
};

// class TCPChannelObserver
// {
// public:
//     // virtual ~TCPChannelObserver() = default;

//     // /* to notify user that its (client) channel is now connected */
//     // virtual void onConnected(TCPChannel*) noexcept = 0;

//     /* tell user that there is new data that can be read, without
//      * saying how much. there might still be existing buffered data
//      * that user has not read. */
//     virtual void onNewReadDataAvailable(TCPChannel*) noexcept = 0;

//     virtual void onWrittenData(TCPChannel*) noexcept {};

//     /* on error or eof, any buffered input data can still be read if
//      * the user wants to.
//      */

//     virtual void onEOF(TCPChannel*) noexcept = 0;
//     /* will be called on any kind of error, whether read or write */
//     virtual void onError(TCPChannel*, int errorcode) noexcept = 0;
// };


/*
 * minimal wrapper around libevent's buffereevent to make it a little
 * bit easier to use
 *
 * cannot directly delete this object; use destroy() api (inherited
 * from DelayedDestruction)
 */
class TCPChannel : public StreamChannel
{
public:
    // for convenience. DelayedDestruction (see folly's
    // AsyncTransport.h for example)
    typedef std::unique_ptr<TCPChannel, /*folly::*/Destructor> UniquePtr;

    /* meant to be used by a client */
    explicit TCPChannel(struct event_base *, StreamChannelObserver*);

    /* meant to be used by server: create a channel from an already
     * established, i.e., accepted, fd. WILL assume the channel is
     * established.
     */
    explicit TCPChannel(struct event_base *, const int fd);
    virtual void set_channel_observer(StreamChannelObserver*) override;


    /* return same value as bufferevent_socket_connect() */
    int start_connecting(const in_addr_t& addr, const in_port_t& port,
                         TCPChannelConnectObserver*);

    /* --------- StreamChannel impl ------------- */
    virtual size_t read(uint8_t *data, size_t size) override;
    virtual int read_buffer(struct evbuffer* buf) override;
    virtual int drain(size_t len) override;
    virtual uint8_t* peek(size_t len) override;

    /* get number of availabe input bytes */
    virtual size_t get_avail_input_length() const override;

    /* same as bufferevent_setwatermark() for read */
    virtual void set_read_watermark(size_t lowmark, size_t highmark) override;

    /* same to output. same as libevent's bufferevent_write calls. in
     * particular, any write before channel is established will be
     * buffered */
    virtual int write(const uint8_t *data, size_t size) override;
    virtual int write_buffer(struct evbuffer *buf) override;

    /* close/disconnect the channel, dropping pending/buffered data if
     * any */
    virtual void close() override;

    // virtual void destroy();

protected:

    // destructor should be private or protected to prevent direct
    // deletion. we're using folly::DelayedDestruction
    virtual ~TCPChannel() = default;

    void _setup_bufev(int fd);

    /* non-virtual on_bufev_event() will call the more specific error
     * sub-methods which are virtual
     */
    virtual void on_bufev_event(struct bufferevent *bev, short events) final;
    virtual void on_bufev_event_connected(struct bufferevent *bev);
    virtual void on_bufev_event_error(struct bufferevent *bev, int errorcode);
    virtual void on_bufev_event_eof(struct bufferevent *bev);


    virtual void on_bufev_read(struct bufferevent *bev);
    virtual void on_bufev_write(struct bufferevent *bev);

    static void s_bufev_eventcb(struct bufferevent *bev, short events, void *ptr);
    static void s_bufev_readcb(struct bufferevent *bev, void *ptr);
    static void s_bufev_writecb(struct bufferevent *bev, void *ptr);

    ////////////////
    struct event_base* evbase_; // don't free
    StreamChannelObserver* observer_; // don't free
    TCPChannelConnectObserver* connect_observer_; // don't free

    enum class ChannelState {
        INIT,
        CONNECTING,
        ESTABLISHED,
        CLOSED /* after either eof or error; can still read buffered
                * input */
    } state_;

    /* initialized to null. client will create new one on connect, and
     * server will create new one on accepted
     */
    std::unique_ptr<struct bufferevent, void(*)(struct bufferevent*)> bufev_;
};


/**************************************************/

// class TCPClientChannel : public TCPChannel
// {
// public:
//     typedef std::unique_ptr<TCPClientChannel, /*folly::*/Destructor> UniquePtr;

//     /* "port" should be in host byte order */
//     explicit TCPClientChannel(struct event_base*,
//                               const in_addr_t& addr, const in_port_t& port,
//                               TCPChannelObserver*);

//     /* return true if successfully initiated a connect attempt. won't
//      * be connected until onConnected() is called. */
//     bool start_connecting(TCPChannelConnectObserver*);

// protected:

//     virtual ~TCPClientChannel();

//     virtual void on_bufev_event_connected(struct bufferevent *) override;
//     virtual void on_bufev_event_error(struct bufferevent *, int errorcode) override;

//     ////////////////

//     const in_addr_t addr_;
//     const in_port_t port_;

//     enum class ConnectState {
//         INIT, CONNECTING, CONNECTED, ERROR
//     } connect_state_;

//     TCPChannelConnectObserver* connect_observer_;
// };


/**************************************************/


} // end myio namespace

#endif /* tcp_channel_hpp */
