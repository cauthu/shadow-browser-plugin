#ifndef generic_ipc_channel_hpp
#define generic_ipc_channel_hpp


// #include <event2/event.h>
// #include <event2/bufferevent.h>
#include <memory>
#include <map>
#include <boost/function.hpp>

#include "../object.hpp"
#include "../timer.hpp"
#include "../stream_channel.hpp"
#include "../generic_message_channel.hpp"

namespace myipc
{

/* msg ids are used to implement msgs that require responses. we refer
 * to these msgs as "calls" (as in function calls). so these are like
 * RPCs
 *
 * non-call msgs have id = 0. if a client makes a call, it must use
 * only odd msg ids; server only even msg ids.
 *
 * the call reply msgs will specify the same id, so that the initiator
 * of the call can know it's a reply
 *
 *
 *    NOTE!!    currently only clients can make calls
 *
 */

class GenericIpcChannel : public Object
                        , public myio::StreamChannelConnectObserver
                        , public myio::GenericMessageChannelObserver
{
public:
    typedef std::unique_ptr<GenericIpcChannel, /*folly::*/Destructor> UniquePtr;

    enum class ChannelStatus : short
    {
        READY,
        CLOSED
    };

    /* status of response to a call() */
    enum class RespStatus : short
    {
        RECV /* received the response */,
        TIMEDOUT /* timed out waiting for resp msg */,
        ERR /* some other error */
    };

    typedef boost::function<void(GenericIpcChannel*, ChannelStatus)> ChannelStatusCb;
    typedef boost::function<void(GenericIpcChannel*, uint8_t type, uint16_t len,
                                 const uint8_t*)> OnMsgCb;

    /* if the status is RECV, then the resp msg can be obtained from
     * the buf pointer. any other status, e.g., TIMEDOUT or ERR, then
     * "len" will be 0 and the buf will be nullptr
     */
    typedef boost::function<void(GenericIpcChannel*, RespStatus status, uint16_t len,
                                 const uint8_t* buf)> OnRespStatusCb;
    /* timed out waiting for response */
    typedef boost::function<void(GenericIpcChannel*)> RespTimeoutCb;
    /* for user of GenericIpcChannel: CalledCb notifies user of a call
     * from the other IPC peer. the "uint32_t id" is the opaque that
     * the user must specify in its response
     */
    typedef boost::function<void(GenericIpcChannel*, uint32_t id, uint8_t type,
                                 uint16_t len, const uint8_t*)> CalledCb;

    /* will take ownership of the stream channel.
     *
     * this is assumed to be an IPC client (i.e., no CalledCb), and
     * will call start_connecting() on the stream channel
     */
    explicit GenericIpcChannel(struct event_base*,
                               myio::StreamChannel::UniquePtr,
                               OnMsgCb, ChannelStatusCb);

    /* will take ownership of the stream channel.
     *
     * this is assumed to be an IPC server (i.e., specified CalledCb)
     */
    explicit GenericIpcChannel(struct event_base*,
                               myio::StreamChannel::UniquePtr,
                               OnMsgCb, CalledCb, ChannelStatusCb);

    void sendMsg(uint8_t type, uint16_t len, const uint8_t* buf);
    void sendMsg(uint8_t type); // send empty msg

    /* make a call to the other peer, expecting a response message of
     * type "resp_type".
     *
     * "timeoutSecs" is optional timeout in seconds waiting for
     * response */
    void call(uint8_t type, uint16_t len, const uint8_t* buf,
              uint8_t resp_type, OnRespStatusCb on_resp_status_cb,
              const uint8_t *timeoutSecs=nullptr);
    /* respond to a call */
    void reply(uint32_t id, uint8_t type, uint16_t len, const uint8_t*);

protected:

    virtual ~GenericIpcChannel() = default;

    /**** implement StreamChannelConnectObserver interface *****/
    virtual void onConnected(myio::StreamChannel*) noexcept override;
    virtual void onConnectError(myio::StreamChannel*, int errorcode) noexcept override;
    virtual void onConnectTimeout(myio::StreamChannel*) noexcept override;

    /**** implement GeneriMessageChannelObserver interface *****/
    virtual void onRecvMsg(myio::GenericMessageChannel*, uint8_t, uint32_t,
                           uint16_t, const uint8_t*) noexcept override;
    virtual void onEOF(myio::GenericMessageChannel*) noexcept override;
    virtual void onError(myio::GenericMessageChannel*, int) noexcept override;

    ////////


    void _on_timeout_waiting_resp(Timer*, uint32_t id);


    struct event_base* evbase_;
    myio::StreamChannel::UniquePtr stream_ch_;
    myio::GenericMessageChannel::UniquePtr gen_msg_ch_;
    const bool is_client_;

    OnMsgCb msg_cb_; /* for notifying user of non-reply msgs */
    ChannelStatusCb channel_status_cb_;
    CalledCb called_cb_;

    uint32_t next_call_msg_id_;
    /* map key is msg id */
    struct CallInfo
    {
        uint8_t call_msg_type; /* save the msg type of the call */
        uint8_t exp_resp_msg_type; /* expected response msg type */
        OnRespStatusCb on_resp_status_cb;
        Timer::UniquePtr timeout_timer;
    };
    std::map<uint32_t, CallInfo> pending_calls_;
};


} // end myipc namespace

#endif /* generic_ipc_channel_hpp */
