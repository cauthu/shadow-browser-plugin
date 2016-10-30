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
#include "../../utility/buflo_mux_channel_impl_spdy.hpp"

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
                        const uint32_t& tamaraw_pkt_intvl_ms,
                        const uint32_t& tamaraw_L,
                        const uint32_t& tamaraw_time_limit_secs,
                        myio::StreamChannel::UniquePtr csp_channel,
                        const bool& log_outer_connect_latency,
                        CSPHandlerDoneCb);

protected:

    virtual ~CSPHandler();

    ////////////

    void _on_buflo_channel_status(myio::buflo::BufloMuxChannel*,
                                  myio::buflo::BufloMuxChannel::ChannelStatus);
    void _on_buflo_new_stream_connect_request(
        myio::buflo::BufloMuxChannel*, int, const char*, uint16_t);
    void _on_stream_handler_done(StreamHandler*);

    void _report_stats() const;

    //////////

    struct event_base* evbase_;
    // myio::buflo::BufloMuxChannelImplSpdy::UniquePtr buflo_channel_;
    myio::buflo::BufloMuxChannelImplSpdy::UniquePtr buflo_channel_;

    const bool log_outer_connect_latency_;

    CSPHandlerDoneCb handler_done_cb_;

    std::map<uint32_t, StreamHandler::UniquePtr> shandlers_;
};

}

#endif /* end CSP_HANDLER_HPP */
