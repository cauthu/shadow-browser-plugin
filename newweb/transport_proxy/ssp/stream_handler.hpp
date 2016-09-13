#ifndef STREAM_HANDLER
#define STREAM_HANDLER

#include "../../utility/object.hpp"
#include "../../utility/tcp_channel.hpp"
#include "../../utility/buflo_mux_channel.hpp"

#include "../../utility/buflo_mux_channel_impl_spdy.hpp"


class StreamHandler;

typedef boost::function<void(StreamHandler*)> StreamHandlerDoneCb;

class StreamHandler : public Object
                    , public myio::StreamChannelObserver
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
                             StreamHandlerDoneCb);


protected:

    virtual ~StreamHandler() = default;

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
                                      bool) override
    {
        LOG(FATAL) << "not reached";
    }
    virtual void onStreamNewDataAvailable(myio::buflo::BufloMuxChannel*) override;
    virtual void onStreamClosed(myio::buflo::BufloMuxChannel*) override;

    /***** implement StreamChannel interface */
    virtual void onNewReadDataAvailable(myio::StreamChannel*) noexcept override;
    virtual void onWrittenData(myio::StreamChannel*) noexcept override;
    virtual void onEOF(myio::StreamChannel*) noexcept override;
    virtual void onError(myio::StreamChannel*, int errorcode) noexcept override;

    //////////////


    struct event_base* evbase_;
    myio::buflo::BufloMuxChannel* buflo_ch_;
    const int sid_;
    StreamHandlerDoneCb handler_done_cb_;

    enum class State {
        CONNECTING, /* to target */
        LINKED,
    } state_;

    // tcp connection to target
    myio::TCPChannel::UniquePtr target_channel_;
};


#endif /* STREAM_HANDLER */
