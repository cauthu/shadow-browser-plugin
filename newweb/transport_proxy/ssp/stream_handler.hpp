#ifndef STREAM_HANDLER
#define STREAM_HANDLER

#include "../../utility/object.hpp"
#include "../../utility/tcp_channel.hpp"
#include "../../utility/buflo_mux_channel.hpp"
#include "../common_inner_outer_handler.hpp"

namespace ssp
{

class StreamHandler;

typedef boost::function<void(StreamHandler*)> StreamHandlerDoneCb;

class StreamHandler : public Object
                    , public myio::StreamChannelConnectObserver
                    , public myio::buflo::BufloMuxChannelStreamObserver
{
public:
    typedef std::unique_ptr<StreamHandler, /*folly::*/Destructor> UniquePtr;

    explicit StreamHandler(struct event_base* evbase,
                             myio::buflo::BufloMuxChannel* buflo_ch,
                             const int sid,
                             const char* target_host,
                             const uint16_t& port,
                           const bool& log_connect_latency,
                             StreamHandlerDoneCb);

protected:

    virtual ~StreamHandler();

    /***** implement StreamChannelConnectObserver interface */
    virtual void onConnected(myio::StreamChannel*) noexcept override;
    virtual void onConnectError(myio::StreamChannel*, int) noexcept override;
    virtual void onConnectTimeout(myio::StreamChannel*) noexcept override;

    /***** implement BufloMuxChannelStreamObserver interface ******/
    virtual void onStreamIdAssigned(myio::buflo::BufloMuxChannel*,
                                    int) override
    {
        LOG(FATAL) << "not reached";
    }
    virtual void onStreamCreateResult(myio::buflo::BufloMuxChannel*,
                                      bool,
                                      const in_addr_t&,
                                      const uint16_t&) override
    {
        LOG(FATAL) << "not reached";
    }
    virtual void onStreamNewDataAvailable(myio::buflo::BufloMuxChannel*, int) noexcept override;
    virtual void onStreamRecvEOF(myio::buflo::BufloMuxChannel*, int) noexcept override {};
    virtual void onStreamClosed(myio::buflo::BufloMuxChannel*, int) noexcept override;

    //////////////

    void _close();
    void _on_inner_outer_handler_done(InnerOuterHandler*, bool);


    struct event_base* evbase_;
    myio::TCPChannel::UniquePtr target_channel_;
    myio::buflo::BufloMuxChannel* buflo_channel_;
    const int sid_;
    InnerOuterHandler::UniquePtr inner_outer_handler_;
    StreamHandlerDoneCb handler_done_cb_;

    const std::string target_host_;
    const uint16_t target_port_;

    enum class State {
        CONNECTING_TARGET,
        FORWARDING /* handled by InnerOuterHandler */,
        CLOSED
    } state_;

    uint64_t connect_start_time_ms_ = 0;
};

}

#endif /* STREAM_HANDLER */
