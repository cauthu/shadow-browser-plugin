#ifndef HANDLER_HPP
#define HANDLER_HPP

#include <string>
#include <queue>
#include <set>

#include "../utility/object.hpp"
#include "../utility/stream_channel.hpp"


class Handler;

class HandlerObserver
{
public:
    virtual void onHandlerDone(Handler*) noexcept = 0;
};

class Handler : public Object
              , public myio::StreamChannelObserver
              , public myio::StreamChannelInputDropObserver
{
public:
    typedef std::unique_ptr<Handler, folly::DelayedDestruction::Destructor> UniquePtr;

    explicit Handler(myio::StreamChannel::UniquePtr channel,
                     HandlerObserver* observer);

private:

    virtual ~Handler();

    /********* StreamChannelObserver interface *************/
    virtual void onNewReadDataAvailable(myio::StreamChannel*) noexcept override;
    virtual void onEOF(myio::StreamChannel*) noexcept override;
    virtual void onError(myio::StreamChannel*, int errorcode) noexcept override;
    virtual void onWrittenData(myio::StreamChannel*) noexcept override {};

    /********* StreamChannelInputDropObserver interface *************/
    virtual void onInputBytesDropped(myio::StreamChannel*, size_t) noexcept override;

    ///////////////////

    void _maybe_consume_input();
    void _serve_response();

    myio::StreamChannel::UniquePtr channel_; // the underlying stream
    HandlerObserver* observer_;

    /* state of reading the request
     *
     * since we only serve dummy response body, we don't a separate
     * state machine for serving the response: it's just one
     * _serve_response() call
     */
    enum class HTTPReqState {
        HTTP_REQ_STATE_REQ_LINE,
        HTTP_REQ_STATE_HEADERS,
        HTTP_REQ_STATE_BODY
    } http_req_state_;

    // we don't support pipelining, so there can be only one active
    // request at a time
    struct RequestInfo
    {
        int active; // this request is currently being processed
        size_t resp_headers_size;
        size_t resp_body_size;
    } current_req_;

    // of the current _request_ we're extracting from client (parsed
    // from content-length header)
    size_t remaining_req_body_length_;

};

#endif /* HANDLER_HPP */
