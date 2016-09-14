#ifndef CLIENT_HANDLER_HPP
#define CLIENT_HANDLER_HPP

#include <memory>
#include <boost/function.hpp>

#include "../../utility/tcp_channel.hpp"
#include "../../utility/stream_channel.hpp"
#include "../../utility/buflo_mux_channel.hpp"
#include "../common_inner_outer_handler.hpp"


/* this takes care of one proxy client (e.g., browser), sitting
 * between the client and the buflo channel
 *
 * it is given a tcp connection to the client, and the buflo
 * channel. it will handle the socks5 request from the client, create
 * the stream with buflo channel, etc. and once the two sides are
 * successfully established and ready to transfer application data, it
 * will hand them off to InnerOuterHandler
 */

class ClientHandler;

typedef boost::function<void(ClientHandler*)> HandlerDoneCb;

class ClientHandler : public Object
                         , public myio::buflo::BufloMuxChannelStreamObserver
                         , public myio::StreamChannelObserver
{
public:
    typedef std::unique_ptr<ClientHandler, /*folly::*/Destructor> UniquePtr;

    explicit ClientHandler(
        myio::StreamChannel::UniquePtr client_channel,
        myio::buflo::BufloMuxChannel* buflo_channel,
        HandlerDoneCb);

protected:

    virtual ~ClientHandler();

    /* implement BufloMuxChannelStreamObserver interface */
    virtual void onStreamIdAssigned(myio::buflo::BufloMuxChannel*, int) noexcept override;
    virtual void onStreamCreateResult(myio::buflo::BufloMuxChannel*, bool) noexcept override;
    virtual void onStreamNewDataAvailable(myio::buflo::BufloMuxChannel*) noexcept override;
    virtual void onStreamClosed(myio::buflo::BufloMuxChannel*) noexcept override;

    /***** implement StreamChannel interface */
    virtual void onNewReadDataAvailable(myio::StreamChannel*) noexcept override;
    virtual void onWrittenData(myio::StreamChannel*) noexcept override;
    virtual void onEOF(myio::StreamChannel*) noexcept override;
    virtual void onError(myio::StreamChannel*, int errorcode) noexcept override;


    ////////////

    void _consume_client_input();
    bool _read_socks5_greeting(size_t);
    bool _read_socks5_connect_req(size_t);
    bool _create_stream(const char* host, uint16_t port);
    void _close(bool);
    void _on_inner_outer_handler_done(InnerOuterHandler*);
    
    myio::StreamChannel::UniquePtr client_channel_;
    myio::buflo::BufloMuxChannel* buflo_channel_;
    int sid_;
    InnerOuterHandler::UniquePtr inner_outer_handler_;

    enum class State {
        READ_SOCKS5_GREETING,
        READ_SOCKS5_CONNECT_REQ,
        CREATE_BUFLO_STREAM,
        FORWARDING /* handled by InnerOuterHandler */,
        CLOSED,
    } state_;
    HandlerDoneCb handler_done_cb_;
};

#endif /* end CLIENT_HANDLER_HPP */
