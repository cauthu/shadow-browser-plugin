#ifndef CSP_HANDLER_HPP
#define CSP_HANDLER_HPP


/* this handles one CSP, given the buflo mux channel connected to that
 * csp
 *
 * when the buflo mux channel notifies me of a new stream connect
 * request, i simply hand it off to a StreamHandler
 */


#include <memory>
#include <boost/function.hpp>

#include "../../utility/tcp_channel.hpp"
#include "../../utility/stream_channel.hpp"
#include "../../utility/buflo_mux_channel.hpp"

// #include "../../utility/buflo_mux_channel_impl_spdy.hpp"


#include "stream_handler.hpp"


namespace ssp
{


class CSPHandler;

typedef boost::function<void(CSPHandler*)> CSPHandlerDoneCb;

class CSPHandler : public Object
{
public:
    typedef std::unique_ptr<CSPHandler, /*folly::*/Destructor> UniquePtr;

    explicit CSPHandler(struct event_base* evbase,
                        myio::StreamChannel::UniquePtr csp_channel,
                        CSPHandlerDoneCb);

protected:

    // /* implement BufloMuxChannelSpdyStreamObserver interface */
    // virtual void onStreamIdAssigned(myio::buflo::BufloMuxChannelImplSpdy*, int) noexcept override;
    // virtual void onStreamCreateResult(myio::buflo::BufloMuxChannelImplSpdy*, bool) noexcept override;
    // virtual void onStreamNewDataAvailable(myio::buflo::BufloMuxChannelImplSpdy*) noexcept override;
    // virtual void onStreamClosed(myio::buflo::BufloMuxChannelImplSpdy*) noexcept override;

    // /***** implement StreamChannelConnectObserver interface */
    // virtual void onConnected(myio::StreamChannel*) noexcept override;
    // virtual void onConnectError(myio::StreamChannel*, int errorcode) noexcept override;
    // virtual void onConnectTimeout(myio::StreamChannel*) noexcept override;


    ////////////

    void _on_buflo_channel_closed(myio::buflo::BufloMuxChannel*);
    void _on_buflo_new_stream_connect_request(
        myio::buflo::BufloMuxChannel*, int, const char*, uint16_t);
    void _on_stream_handler_done(StreamHandler*);

    struct event_base* evbase_;
    // myio::buflo::BufloMuxChannelImplSpdy::UniquePtr buflo_channel_;
    myio::buflo::BufloMuxChannel::UniquePtr buflo_channel_;

    CSPHandlerDoneCb handler_done_cb_;

    std::map<uint32_t, StreamHandler::UniquePtr> shandlers_;
};

}

#endif /* end CSP_HANDLER_HPP */
