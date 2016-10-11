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

namespace csp
{

class ClientHandler;

typedef boost::function<void(ClientHandler*)> ClientHandlerDoneCb;

class ClientHandler : public Object
                         , public myio::buflo::BufloMuxChannelStreamObserver
                         , public myio::StreamChannelObserver
{
public:
    typedef std::unique_ptr<ClientHandler, /*folly::*/Destructor> UniquePtr;

    explicit ClientHandler(
        myio::StreamChannel::UniquePtr client_channel,
        myio::buflo::BufloMuxChannel* buflo_channel,
        ClientHandlerDoneCb);

protected:

    virtual ~ClientHandler();

    /* implement BufloMuxChannelStreamObserver interface */
    virtual void onStreamIdAssigned(myio::buflo::BufloMuxChannel*, int) noexcept override;
    virtual void onStreamCreateResult(myio::buflo::BufloMuxChannel*,
                                      bool,
                                      const in_addr_t&,
                                      const uint16_t&) noexcept override;
    virtual void onStreamNewDataAvailable(myio::buflo::BufloMuxChannel*, int) noexcept override;
    virtual void onStreamClosed(myio::buflo::BufloMuxChannel*, int) noexcept override;
    virtual void onStreamRecvEOF(myio::buflo::BufloMuxChannel*, int) noexcept override;

    /***** implement StreamChannel interface */
    virtual void onNewReadDataAvailable(myio::StreamChannel*) noexcept override;
    virtual void onWrittenData(myio::StreamChannel*) noexcept override;
    virtual void onEOF(myio::StreamChannel*) noexcept override;
    virtual void onError(myio::StreamChannel*, int errorcode) noexcept override;


    ////////////

    void _consume_client_input();
    bool _read_socks5_greeting(size_t);
    bool _read_socks5_connect_req(size_t);
    void _on_inner_outer_handler_done(InnerOuterHandler*, bool);
    void _write_socks5_connect_request_granted();
    void _close();

    
    myio::StreamChannel::UniquePtr client_channel_;
    myio::buflo::BufloMuxChannel* buflo_channel_;
    int sid_;
    InnerOuterHandler::UniquePtr inner_outer_handler_;
    ClientHandlerDoneCb handler_done_cb_;

    enum class State {
        READ_SOCKS5_GREETING,
        READ_SOCKS5_CONNECT_REQ,
        CREATE_BUFLO_STREAM,
        FORWARDING /* handled by InnerOuterHandler */,
        CLOSED,
    } state_;
};


} // namespace csp

#endif /* end CLIENT_HANDLER_HPP */
