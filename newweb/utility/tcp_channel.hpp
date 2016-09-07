#ifndef tcp_channel_hpp
#define tcp_channel_hpp


#include <memory>
#include <functional>

#include "DelayedDestruction.h"

#include "object.hpp"
#include "stream_channel.hpp"

namespace myio
{

class TCPChannel;


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
    typedef std::unique_ptr<TCPChannel, folly::DelayedDestruction::Destructor> UniquePtr;

    /* meant to be used by a client. port is in HOST byte order */
    explicit TCPChannel(struct event_base *,
                        const in_addr_t& addr,
                        const in_port_t& port,
                        StreamChannelObserver*);

    /* meant to be used by server: create a channel from an already
     * established, i.e., accepted, fd. WILL assume the channel is
     * established.
     */
    explicit TCPChannel(struct event_base *, const int fd);

    virtual void set_observer(StreamChannelObserver*) override;

    /* --------- StreamChannel impl ------------- */
    /* return same value as bufferevent_socket_connect() */
    virtual int start_connecting(StreamChannelConnectObserver*,
                                 struct timeval *connect_timeout=nullptr) override;

    virtual int read(uint8_t *data, size_t len) override;
    virtual int read_buffer(struct evbuffer* buf, size_t len) override;
    virtual int drain(size_t len) override;
    virtual uint8_t* peek(ssize_t len) override;

    virtual struct evbuffer* get_input_evbuf() override { return input_evb_.get(); }

    virtual void drop_future_input(StreamChannelInputDropObserver*, size_t) override;

    /* get number of availabe input bytes */
    virtual size_t get_avail_input_length() const override;
    virtual size_t get_output_length() const override;

    /* same as bufferevent_setwatermark() for read */
    virtual void set_read_watermark(size_t lowmark, size_t highmark) override;

    /* same to output. same as libevent's bufferevent_write calls. in
     * particular, any write before channel is established will be
     * buffered */
    virtual int write(const uint8_t *data, size_t len) override;
    virtual int write_buffer(struct evbuffer *buf) override;

    /* write "len" bytes of dummy data */
    virtual int write_dummy(size_t len) override;

    /* close/disconnect the channel, dropping pending/buffered data if
     * any */
    virtual void close() override;

    virtual bool is_closed() const override;

protected:

    enum class ChannelState {
        INIT,
        CONNECTING_SOCKET,
        SOCKET_CONNECTED,
        CLOSED /* after either eof or error; can still read buffered
                * input */
    };

    TCPChannel(struct event_base *evbase, int fd,
               const in_addr_t& addr, const in_port_t& port,
               StreamChannelObserver* observer,
               ChannelState starting_state, bool is_client);

    // destructor should be private or protected to prevent direct
    // deletion. we're using folly::DelayedDestruction
    virtual ~TCPChannel() = default;

    void _initialize_read_write_events();
    void _set_read_monitoring(bool);
    /* shadow doesn't support edge-triggered (epoll) monitoring, so we
     * have to disable write monitoring if we don't have data to
     * write, otherwise will keep getting notified of the write event
     */
    void _maybe_toggle_write_monitoring(bool force_enable=false);
    void _on_eof();
    void _on_error();
    void _on_socket_connect_eventcb(int fd, short what);
    void _on_socket_readcb(int fd, short what);
    void _on_socket_writecb(int fd, short what);

    /* (maybe) read and drop bytes from socket, so they dont get copied into
     * input buffer.
     *
     * returns false if NO more can be read from socket, e.g., due to
     * eof, error, etc. thus if true is returned, the socket might
     * have more data that can be read.
     */
    bool _maybe_dropread();

    static void s_socket_connect_eventcb(int fd, short what, void* arg);
    static void s_socket_readcb(int fd, short what, void* arg);
    static void s_socket_writecb(int fd, short what, void* arg);

    ////////////////////////////////////////////////

    struct event_base* evbase_; // don't free
    StreamChannelObserver* observer_; // don't free
    StreamChannelConnectObserver* connect_observer_; // don't free

    ChannelState state_;

    int fd_;
    /* using separate events for read and write, so it's easy to
     * enable/disable
     */
    std::unique_ptr<struct event, void(*)(struct event*)> socket_read_ev_;
    std::unique_ptr<struct event, void(*)(struct event*)> socket_write_ev_;

    const in_addr_t addr_;
    const in_port_t port_;
    const bool is_client_;

    std::unique_ptr<struct evbuffer, void(*)(struct evbuffer*)> input_evb_;
    std::unique_ptr<struct evbuffer, void(*)(struct evbuffer*)> output_evb_;

    // read low-water mark: if a socket read makes input buffer
    // contain at least this many bytes, then we notify user's
    // onNewReadDataAvailable()
    size_t read_lw_mark_;

    /* drop this many bytes from the input socket */
    struct InputDropRequest
    {
        StreamChannelInputDropObserver* observer;
        size_t num_requested;
        size_t num_remaining;
    } input_drop_req_;

    /* timer to send at constant rate */
    std::unique_ptr<struct event, void(*)(struct event*)> tamaraw_timer_ev_;
};


/**************************************************/

} // end myio namespace

#endif /* tcp_channel_hpp */
