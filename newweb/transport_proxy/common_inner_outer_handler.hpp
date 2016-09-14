#ifndef COMMON_INNER_OUTER_HANDLER_HPP
#define COMMON_INNER_OUTER_HANDLER_HPP

#include <memory>
#include <boost/function.hpp>

#include "../utility/object.hpp"
#include "../utility/easylogging++.h"
#include "../utility/stream_channel.hpp"
#include "../utility/buflo_mux_channel.hpp"


/* this takes care of forwarding data betwee a pair of streams: inner
 * stream is in the buflo channel, and outer stream is tcpstream to
 * either client or server.
 *
 * both streams must have been fully established and ready to transfer
 * application data, e.g., on client side, the socks handshake must
 * have been finished
 *
 * this handler does NOT take ownership of the streams, so it won't do
 * any actual closing of either stream. it just notifies its handler
 * when either stream closes.
 *
 */

class InnerOuterHandler;

/* bool specifies whether the inner stream's closure is the cause for
 * the handler to be done, i.e., the person being notified might not
 * want/need to close the inner stream
 */
typedef boost::function<void(InnerOuterHandler*, bool)> InnerOuterHandlerDoneCb;

class InnerOuterHandler : public Object
                        , public myio::buflo::BufloMuxChannelStreamObserver
                        , public myio::StreamChannelObserver
{
public:
    typedef std::unique_ptr<InnerOuterHandler, /*folly::*/Destructor> UniquePtr;

    explicit InnerOuterHandler(
        myio::StreamChannel* outer_channel,
        int inner_sid,
        myio::buflo::BufloMuxChannel* buflo_channel,
        InnerOuterHandlerDoneCb);

protected:

    virtual ~InnerOuterHandler() = default;

    /* implement BufloMuxChannelStreamObserver interface */
    virtual void onStreamIdAssigned(myio::buflo::BufloMuxChannel*, int) noexcept override
    {
        LOG(FATAL) << "not reached";
    }
    virtual void onStreamCreateResult(myio::buflo::BufloMuxChannel*,
                                      bool,
                                      const in_addr_t&,
                                      const uint16_t&) noexcept override
    {
        LOG(FATAL) << "not reached";
    }
    virtual void onStreamNewDataAvailable(myio::buflo::BufloMuxChannel*) noexcept override;
    virtual void onStreamClosed(myio::buflo::BufloMuxChannel*) noexcept override;

    /***** implement StreamChannel interface */
    virtual void onNewReadDataAvailable(myio::StreamChannel*) noexcept override;
    virtual void onWrittenData(myio::StreamChannel*) noexcept override;
    virtual void onEOF(myio::StreamChannel*) noexcept override;
    virtual void onError(myio::StreamChannel*, int errorcode) noexcept override;


    ////////////

    void _consume_data_from_outer();
    void _be_done(bool inner_stream_already_closed=false);
    bool _forward_client_data(const size_t& num_avail_bytes);

    myio::StreamChannel* outer_channel_;
    myio::buflo::BufloMuxChannel* buflo_channel_;
    const int inner_sid_;

    InnerOuterHandlerDoneCb handler_done_cb_;
};

#endif /* end COMMON_INNER_OUTER_HANDLER_HPP */
